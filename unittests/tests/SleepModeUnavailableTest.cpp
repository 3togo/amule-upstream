// This file is part of the aMule Project.
// Copyright (c) 2003-2026 aMule Team ( https://amule-org.github.io )
// Licensed under the GNU General Public License, version 2 or later.

#include "PlatformSpecific.h"

static_assert(
	PLATFORMSPECIFIC_CAN_PREVENT_SLEEP_MODE == 0, "Unavailable builds must keep the preference disabled");

int main()
{
	PlatformSpecific::PreventSleepMode(true);
	PlatformSpecific::PreventSleepMode(false);
	PlatformSpecific::AllowSleepMode();
	PlatformSpecific::ShutdownSleepMode();
	PlatformSpecific::ShutdownSleepMode();
	return 0;
}
