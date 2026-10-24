//
// This file is part of the aMule Project.
//
// Copyright (c) 2003-2026 aMule Team ( https://amule-org.github.io )
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

#include "config.h"
#include "UploadStorageMap.h"
#include "StorageService.h"
#include "StorageIdentity.h"
#include <fstream>
#include <set>
#include <sys/stat.h>
#include <cstdio>
#include <cstdlib>
#include <cwchar>

#if defined(_WIN32)
#include <wx/string.h>
#include <windows.h>
#include <winnetwk.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#include <sys/param.h>
#include <sys/mount.h>
#endif

#ifdef __linux__
namespace
{
std::string BackingDevice(const std::string &device, unsigned depth = 0)
{
	if (depth >= 8) {
		return "unknown";
	}
	std::error_code error;
	auto path = std::filesystem::canonical("/sys/dev/block/" + device, error);
	if (error) {
		return "unknown";
	}
	if (std::filesystem::exists(path / "partition", error)) {
		path = path.parent_path();
	}
	std::set<std::string> disks;
	for (std::filesystem::directory_iterator it(path / "slaves", error), end; !error && it != end;
		it.increment(error)) {
		std::string child;
		std::ifstream input(it->path() / "dev");
		input >> child;
		if (child.empty() || disks.size() >= 32) {
			return "unknown";
		}
		disks.insert(BackingDevice(child, depth + 1));
	}
	if (error && error != std::errc::no_such_file_or_directory) {
		return "unknown";
	}
	if (!disks.empty()) {
		return disks.size() == 1 ? *disks.begin() : "unknown";
	}
	std::string parent;
	std::ifstream input(path / "dev");
	input >> parent;
	return parent.empty() ? "unknown" : "disk:" + parent;
}
} // namespace
#endif

bool UploadStorageMap::Refresh([[maybe_unused]] bool includeNetwork)
{
	UploadStorageMap next;
#ifdef __linux__
	std::map<std::string, std::string> aliases;
#endif
#if defined(__linux__)
	// mountinfo contains device IDs and mount points without probing any volume.
	if (FILE *file = std::fopen("/proc/self/mountinfo", "r")) {
		char *line = nullptr;
		size_t size = 0;
		while (::getline(&line, &size, file) >= 0) {
			next.AddLinuxMount(line);
			std::istringstream fields(line);
			std::string id, parent, device;
			fields >> id >> parent >> device;
			if (!device.empty()) {
				const std::string key = "device:" + device;
				if (aliases.count(key) != 0) {
					continue;
				}
				const std::string entry(line);
				const bool memory = entry.find(" - tmpfs ") != std::string::npos ||
						    entry.find(" - ramfs ") != std::string::npos;
				auto backing = memory ? key : BackingDevice(device);
				// Btrfs can expose an anonymous st_dev while its mount source
				// names the block device. Resolve that source on this worker.
				std::string token, type, source;
				while (fields >> token && token != "-") {
				}
				fields >> type >> source;
				if (!memory && backing == "unknown" && source.compare(0, 5, "/dev/") == 0) {
					struct stat info
					{
					};
					if (::stat(source.c_str(), &info) == 0 && S_ISBLK(info.st_mode)) {
						backing = BackingDevice(
							StorageDeviceIdentity(info.st_rdev).substr(7));
					}
				}
				aliases[key] = std::move(backing);
			}
		}
		std::free(line);
		std::fclose(file);
	}
#elif defined(_WIN32)
	wchar_t volume[MAX_PATH];
	HANDLE search = FindFirstVolumeW(volume, MAX_PATH);
	if (search != INVALID_HANDLE_VALUE) {
		do {
			DWORD size = 0;
			GetVolumePathNamesForVolumeNameW(volume, nullptr, 0, &size);
			if (size == 0) {
				continue;
			}
			std::vector<wchar_t> paths(size);
			if (GetVolumePathNamesForVolumeNameW(volume, paths.data(), size, &size)) {
				for (const wchar_t *path = paths.data(); *path;
					path += std::wcslen(path) + 1) {
					wxString normalized(path);
					normalized.Replace("\\", "/");
					next.Add(normalized.Lower().ToStdString(wxConvUTF8),
						wxString(volume).ToStdString(wxConvUTF8));
				}
			}
		} while (FindNextVolumeW(search, volume, MAX_PATH));
		FindVolumeClose(search);
	}
	const DWORD drives = GetLogicalDrives();
	for (unsigned index = 0; index < 26; ++index) {
		if (!includeNetwork || (drives & (1UL << index)) == 0) {
			continue;
		}
		const wchar_t drive[] = { static_cast<wchar_t>(L'A' + index), L':', L'\0' };
		const auto local = wxString(drive).Lower().ToStdString(wxConvUTF8);
		if (next.Lookup(local + "/", true) != "unknown") {
			continue; // already covered by the local volume GUID table
		}
		std::vector<wchar_t> remote(MAX_PATH);
		DWORD size = static_cast<DWORD>(remote.size());
		DWORD result = WNetGetConnectionW(drive, remote.data(), &size);
		if (result == ERROR_MORE_DATA && size > 0) {
			remote.resize(size);
			result = WNetGetConnectionW(drive, remote.data(), &size);
		}
		if (result == NO_ERROR) {
			next.AddWindowsNetworkDrive(
				local, wxString(remote.data()).Lower().ToStdString(wxConvUTF8));
		}
	}
#elif defined(__APPLE__) || defined(__FreeBSD__)
	struct statfs *mounts = nullptr;
	const int count = getmntinfo(&mounts, MNT_NOWAIT);
	for (int i = 0; i < count; ++i) {
		struct stat info
		{
		};
		if (::stat(mounts[i].f_mntonname, &info) == 0) {
			next.Add(mounts[i].f_mntonname, StorageDeviceIdentity(info.st_dev));
		}
	}
#ifdef __APPLE__
	next.MergeMacDataVolume();
#endif
#endif
	// Keep the previous snapshot on a transient discovery failure. Unsupported OSes
	// use the conservative unknown group rather than inventing per-directory limits.
	if (!next.m_mounts.empty()) {
		m_mounts = std::move(next.m_mounts);
#ifdef __linux__
		StorageService::Get().SetAliases(std::move(aliases));
#endif
		return true;
	}
	return false;
}
