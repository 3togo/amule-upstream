// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef STORAGEIDENTITY_H
#define STORAGEIDENTITY_H
#include <cstdint>
#include <string>
#ifdef __linux__
#include <sys/types.h>
#include <sys/sysmacros.h>
#endif
inline std::string StorageDeviceIdentity(uint64_t device)
{
#ifdef __linux__
	const auto value = static_cast<dev_t>(device);
	return "device:" + std::to_string(major(value)) + ":" + std::to_string(minor(value));
#else
	return "device:" + std::to_string(device);
#endif
}
#endif
