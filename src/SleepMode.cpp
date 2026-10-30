//
// This file is part of the aMule Project.
//
// Copyright (c) 2003-2026 aMule Team ( https://amule-org.github.io )
//
// Any parts of this program derived from the xMule, lMule or eMule project,
// or contributed by third-party developers are copyrighted by their
// respective authors.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA
//

#include "PlatformSpecific.h"

#if AMULE_CAN_PREVENT_SLEEP
#include "Logger.h"
#include "common/Format.h"
#include <wakeguard/wakeguard.hpp>
#include <chrono>
#include <exception>
#include <memory>
#include <wx/utils.h>

namespace
{
// These entry points are called by the core's main thread. The library worker
// owns all bus/native requests; it never calls the application's logger or UI.
std::unique_ptr<wakeguard::controller> sleepController;
bool sleepShutdown = false;
auto nextControllerAttempt = std::chrono::steady_clock::time_point::min();

void PollSleepEvents()
{
	if (!sleepController) {
		return;
	}
	wakeguard::controller_status status;
	if (!sleepController->take_event(status)) {
		return;
	}
	if (status.error != WG_OK) {
		AddDebugLogLineN(logGeneral,
			CFormat("Sleep prevention unavailable: %s") %
				wxString::FromUTF8(status.detail.c_str()));
	} else if (status.state == wakeguard::phase::active) {
		AddDebugLogLineN(logGeneral,
			CFormat("Sleep prevention acquired via %s (%s).") %
				wxString::FromUTF8(wg_backend_name(status.backend)) %
				(status.scope == WG_SCOPE_SUSPEND ? "suspend" : "idle system"));
	}
}
} // namespace
#endif

void PlatformSpecific::PreventSleepMode(bool hasGui)
{
#if AMULE_CAN_PREVENT_SLEEP
	if (sleepShutdown) {
		return;
	}
	if (!sleepController) {
		const auto now = std::chrono::steady_clock::now();
		if (now < nextControllerAttempt) {
			return;
		}
		nextControllerAttempt = now + std::chrono::seconds(30);
		try {
			auto options = wg_default_options();
			options.application = "aMule";
			options.reason = "Transferring files";
			// A daemon launched in a desktop session needs the same inhibitor
			// route as the GUI; a system service without a desktop stays idle-only.
			// Keep docs/README.md's power-policy description aligned with this choice.
			wxString desktop;
			const bool desktopSession =
				wxGetEnv("XDG_CURRENT_DESKTOP", &desktop) && !desktop.empty();
			options.allow_suspend_block = (hasGui || desktopSession) ? 1 : 0;
			sleepController = std::make_unique<wakeguard::controller>(options);
		} catch (const std::exception &error) {
			AddDebugLogLineN(logGeneral,
				CFormat("Sleep prevention worker unavailable: %s") %
					wxString::FromUTF8(error.what()));
			return;
		}
	}
	sleepController->set_required(true);
	PollSleepEvents();
#else
	(void)hasGui;
#endif
}

void PlatformSpecific::AllowSleepMode()
{
#if AMULE_CAN_PREVENT_SLEEP
	if (sleepController) {
		sleepController->set_required(false);
		PollSleepEvents();
	}
	nextControllerAttempt = std::chrono::steady_clock::time_point::min();
#endif
}

void PlatformSpecific::ShutdownSleepMode()
{
#if AMULE_CAN_PREVENT_SLEEP
	sleepShutdown = true;
	if (sleepController) {
		sleepController->shutdown();
		sleepController.reset();
	}
#endif
}
