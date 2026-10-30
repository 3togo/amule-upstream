// This file is part of the aMule Project.
// Copyright (c) 2003-2026 aMule Team ( https://amule-org.github.io )
// Licensed under the GNU General Public License, version 2 or later.

#include "PlatformSpecific.h"
#include <iostream>
#include <string>

int main(int argc, char **argv)
{
	const bool desktop = argc == 2 && std::string(argv[1]) == "desktop";
	std::cout << "READY\n" << std::flush;
	std::string command;
	while (std::getline(std::cin, command)) {
		if (command == "on") {
			for (int i = 0; i < 1000; ++i) {
				PlatformSpecific::PreventSleepMode(desktop);
			}
		} else if (command == "off") {
			PlatformSpecific::AllowSleepMode();
		} else if (command == "quit") {
			break;
		}
		std::cout << "ACK\n" << std::flush;
	}
	PlatformSpecific::ShutdownSleepMode();
	std::cout << "STOPPED\n" << std::flush;
	return 0;
}
