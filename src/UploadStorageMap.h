// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef UPLOADSTORAGEMAP_H
#define UPLOADSTORAGEMAP_H

#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

// Snapshot of the OS mount table. Lookups never stat/open a shared path, including
// a missing or unreachable mount. Bind mounts / volume aliases share one key.
class UploadStorageMap
{
public:
	void Refresh();
	void Add(std::string path, std::string key)
	{
		while (path.size() > 1 && path.back() == '/') {
			path.pop_back();
		}
		m_mounts.push_back({ std::move(path), std::move(key) });
	}
	void AddLinuxMount(const std::string &line)
	{
		std::istringstream input(line);
		std::string id, parent, device, root, path;
		if (!(input >> id >> parent >> device >> root >> path) ||
			device.find(':') == std::string::npos) {
			return;
		}
		std::string decoded;
		for (size_t i = 0; i < path.size(); ++i) {
			if (path[i] == '\\' && i + 3 < path.size() &&
				(path.compare(i, 4, "\\040") == 0 || path.compare(i, 4, "\\011") == 0 ||
					path.compare(i, 4, "\\012") == 0 ||
					path.compare(i, 4, "\\134") == 0)) {
				decoded += static_cast<char>((path[i + 1] - '0') * 64 +
							     (path[i + 2] - '0') * 8 + path[i + 3] - '0');
				i += 3;
			} else {
				decoded += path[i];
			}
		}
		Add(std::move(decoded), "device:" + device);
	}
	std::string Lookup(const std::string &input, bool windows = false) const
	{
		std::string path = std::filesystem::u8path(input).lexically_normal().generic_u8string();
		if (windows && input.compare(0, 2, "//") == 0 && path.compare(0, 2, "//") != 0) {
			path.insert(0, "/");
		}
		const Mount *best = nullptr;
		for (const auto &mount : m_mounts) {
			if (path.compare(0, mount.path.size(), mount.path) == 0 &&
				(mount.path == "/" || path.size() == mount.path.size() ||
					path[mount.path.size()] == '/') &&
				(best == nullptr || mount.path.size() > best->path.size())) {
				best = &mount;
			}
		}
		if (best != nullptr) {
			return best->key;
		}
		// UNC shares are not in Windows' local volume table. Group by server/share,
		// not by directory. Unknown paths conservatively share one worker permit.
		if (windows && path.compare(0, 2, "//") == 0) {
			const auto server = path.find('/', 2);
			if (server != std::string::npos) {
				return "unc:" + path.substr(0, path.find('/', server + 1));
			}
		}
		return "unknown";
	}

private:
	struct Mount
	{
		std::string path;
		std::string key;
	};
	std::vector<Mount> m_mounts;
};
#endif
