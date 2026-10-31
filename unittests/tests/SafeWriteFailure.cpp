// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later
// Linux-only fault injection; never linked into production executables.
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <limits.h>
#include <unistd.h>

extern "C" int close(int descriptor)
{
	using Close = int (*)(int);
	const auto original = reinterpret_cast<Close>(dlsym(RTLD_NEXT, "close"));
	if (!original) {
		errno = ENOSYS;
		return -1;
	}
	bool blocked = false;
	if (const char *target = std::getenv("AMULE_TEST_SAFE_WRITE_TARGET")) {
		char link[64];
		char path[PATH_MAX];
		std::snprintf(link, sizeof(link), "/proc/self/fd/%d", descriptor);
		const auto size = readlink(link, path, sizeof(path) - 1);
		if (size >= 0) {
			path[size] = '\0';
			blocked = std::strcmp(path, target) == 0;
		}
	}
	const int result = original(descriptor);
	if (blocked && result == 0) {
		const char *marker = std::getenv("AMULE_TEST_SAFE_WRITE_MARKER");
		if (marker) {
			if (FILE *file = std::fopen(marker, "w")) {
				std::fputs("blocked\n", file);
				std::fclose(file);
			}
		}
		// The descriptor is really closed; only its reported result is injected.
		errno = EIO;
		return -1;
	}
	return result;
}
