// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later
#include <muleunit/test.h>
#include "UploadStorageMap.h"

using namespace muleunit;
DECLARE_SIMPLE(UploadStorageMap)

TEST(UploadStorageMap, LongestMountAndDirectoryBoundaries)
{
	UploadStorageMap map;
	map.Add("/", "root");
	map.Add("/media/disk", "disk");
	map.Add("/media/disk/nested", "nested");
	ASSERT_EQUALS(std::string("disk"), map.Lookup("/media/disk/a.bin"));
	ASSERT_EQUALS(std::string("nested"), map.Lookup("/media/disk/nested/a.bin"));
	ASSERT_EQUALS(std::string("root"), map.Lookup("/media/diskette/a.bin"));
	ASSERT_EQUALS(std::string("root"), map.Lookup("/media/disk/../other/a.bin"));
}

TEST(UploadStorageMap, BindMountsShareTheDeviceBudgetAndMountEscapesDecode)
{
	UploadStorageMap map;
	map.AddLinuxMount("1 0 8:1 / /media/drive\\040one rw - ext4 /dev/example rw");
	map.AddLinuxMount("2 0 8:1 /subdir /alias rw - ext4 /dev/example rw");
	map.AddLinuxMount("3 0 8:2 / /other rw - ext4 /dev/other rw");
	ASSERT_EQUALS(map.Lookup("/media/drive one/a.bin"), map.Lookup("/alias/b.bin"));
	ASSERT_FALSE(map.Lookup("/alias/b.bin") == map.Lookup("/other/a.bin"));
}

TEST(UploadStorageMap, UnmappedPathsAreConservativeAndUncSharesAreGrouped)
{
	UploadStorageMap map;
	ASSERT_EQUALS(std::string("unknown"), map.Lookup("/unmapped/a.bin"));
	ASSERT_EQUALS(map.Lookup("//server/share/a/b.bin", true), map.Lookup("//server/share/c.bin", true));
	ASSERT_FALSE(map.Lookup("//server/share/a.bin", true) == map.Lookup("//server/other/a.bin", true));
}
