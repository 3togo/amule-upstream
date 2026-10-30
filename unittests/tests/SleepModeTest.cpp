// This file is part of the aMule Project.
// Copyright (c) 2003-2026 aMule Team ( https://amule-org.github.io )
// Licensed under the GNU General Public License, version 2 or later.

#include "PlatformSpecific.h"
#include "internal.hpp"
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <wx/utils.h>

namespace
{
std::mutex lock;
std::condition_variable changed;
int attempts = 0;
int releases = 0;
bool blocked = true;
std::string application;
std::string reason;
int broaderScope = -1;

struct FakeGuard final : wg_guard
{
	~FakeGuard() override
	{
		std::lock_guard<std::mutex> guard(lock);
		++releases;
		changed.notify_all();
	}
	wg_status check(std::string &) const override { return WG_OK; }
};

bool WaitFor(const std::function<bool()> &condition)
{
	std::unique_lock<std::mutex> guard(lock);
	return changed.wait_for(guard, std::chrono::seconds(5), condition);
}
} // namespace

wg_guard *wg_platform_acquire(const wg_options &options, std::string &)
{
	std::unique_lock<std::mutex> guard(lock);
	++attempts;
	application = options.application;
	reason = options.reason;
	broaderScope = options.allow_suspend_block;
	changed.notify_all();
	changed.wait(guard, [] { return !blocked; });
	auto *result = new FakeGuard;
	result->backend = WG_BACKEND_LOGIND;
	return result;
}

int main(int argc, char **argv)
{
	const bool desktop = argc == 2 && std::string(argv[1]) == "desktop";
	const bool desktopSession = argc == 2 && std::string(argv[1]) == "daemon-desktop";
	wxSetEnv("XDG_CURRENT_DESKTOP", desktopSession ? "XFCE" : "");
	if (!PLATFORMSPECIFIC_CAN_PREVENT_SLEEP_MODE) {
		return 1;
	}
	// A pending acquire must not make a tick or disable block on the backend.
	for (int i = 0; i < 1000; ++i) {
		PlatformSpecific::PreventSleepMode(desktop);
	}
	if (!WaitFor([] { return attempts == 1; })) {
		return 2;
	}
	PlatformSpecific::AllowSleepMode();
	{
		std::lock_guard<std::mutex> guard(lock);
		if (application != "aMule" || reason != "Transferring files" ||
			broaderScope != ((desktop || desktopSession) ? 1 : 0)) {
			return 3;
		}
		blocked = false;
		changed.notify_all();
	}
	if (!WaitFor([] { return releases == 1; })) {
		return 4;
	}
	for (int i = 0; i < 1000; ++i) {
		PlatformSpecific::PreventSleepMode(desktop);
	}
	if (!WaitFor([] { return attempts == 2; })) {
		return 5;
	}
	PlatformSpecific::ShutdownSleepMode();
	PlatformSpecific::ShutdownSleepMode();
	PlatformSpecific::PreventSleepMode(desktop);
	PlatformSpecific::AllowSleepMode();
	std::lock_guard<std::mutex> guard(lock);
	if (attempts != 2 || releases != 2) {
		return 6;
	}
	std::puts("Production sleep adapter: metadata, tick coalescing, late release and shutdown passed");
	return 0;
}
