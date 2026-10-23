// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later
// Linux-only LD_PRELOAD fixture. Gate real reads of one synthetic shared file,
// after startup hashing, without adding delay hooks to the production daemon.
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits.h>
#include <poll.h>
#include <sys/syscall.h>
#include <unistd.h>

extern "C" ssize_t read(int fd, void *buffer, size_t count)
{
	static std::atomic<bool> disabled{ false };
	const char *arm = std::getenv("AMULE_TEST_READ_ARM");
	const char *path = std::getenv("AMULE_TEST_READ_PATH");
	if (!disabled && arm && path && access(arm, F_OK) == 0) {
		char link[64];
		char actual[PATH_MAX];
		std::snprintf(link, sizeof(link), "/proc/self/fd/%d", fd);
		const ssize_t length = readlink(link, actual, sizeof(actual) - 1);
		if (length >= 0) {
			actual[length] = '\0';
			if (std::strcmp(actual, path) == 0) {
				const char *notify = std::getenv("AMULE_TEST_READ_NOTIFY_FD");
				const char *release = std::getenv("AMULE_TEST_READ_RELEASE_FD");
				if (!notify || !release) {
					_exit(98);
				}
				const auto offset = static_cast<std::uint64_t>(lseek(fd, 0, SEEK_CUR));
				if (write(std::atoi(notify), &offset, sizeof(offset)) != sizeof(offset)) {
					_exit(98);
				}
				pollfd gate{ std::atoi(release), POLLIN, 0 };
				int ready;
				do {
					ready = poll(&gate, 1, 30000);
				} while (ready < 0 && errno == EINTR);
				if (ready <= 0) {
					_exit(98);
				}
				char token;
				ssize_t released;
				do {
					released = syscall(SYS_read, gate.fd, &token, 1);
				} while (released < 0 && errno == EINTR);
				if (released == 0) {
					// Closing the parent pipe releases all remaining reads for teardown.
					disabled = true;
				} else if (released != 1) {
					_exit(98);
				}
			}
		}
	}
	return syscall(SYS_read, fd, buffer, count);
}
