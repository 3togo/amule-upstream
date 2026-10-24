// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later
#include <muleunit/test.h>
#include "FileArea.h"
#include "FileAutoClose.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstring>

std::atomic<uint32> TheTime{ 0 };

#if defined(AMULE_TEST_WRAP_MSYNC) && defined(MMAP_SUPPORTED)
namespace
{
bool failSync = false;
unsigned syncCalls = 0;
} // namespace
extern "C" int __real_msync(void *address, size_t length, int flags);
extern "C" int __wrap_msync(void *address, size_t length, int flags)
{
	++syncCalls;
	if (failSync) {
		errno = EIO;
		return -1;
	}
	return __real_msync(address, length, flags);
}
#endif

using namespace muleunit;
namespace
{
const CPath &TestFile()
{
	static const CPath path("FileAreaTest.dat");
	return path;
}
} // namespace

DECLARE(FileArea);
void setUp()
{
	CFileArea::SetMMapEnabled(false);
	CFile file(TestFile(), CFile::write);
	std::array<char, 8192> data{};
	file.Write(data.data(), data.size());
}
void tearDown()
{
#if defined(AMULE_TEST_WRAP_MSYNC) && defined(MMAP_SUPPORTED)
	failSync = false;
#endif
	CFileArea::SetMMapEnabled(false);
	CPath::RemoveFile(TestFile());
}
END_DECLARE;

TEST(FileArea, BufferedWriteRoundTrip)
{
	CFileAutoClose file(TestFile(), CFile::read_write);
	CFileArea area;
	area.StartWriteAt(file, 17, 23);
	std::memset(area.GetBuffer(), 0x5A, 23);
	area.FlushAt(file, 17, 23);
	ASSERT_TRUE(area.GetBuffer() == nullptr);
	std::array<unsigned char, 40> data{};
	file.ReadAt(data.data(), 0, data.size());
	for (size_t i = 0; i < data.size(); ++i) {
		ASSERT_EQUALS(i < 17 ? 0 : 0x5A, static_cast<int>(data[i]));
	}
}

TEST(FileArea, BufferedWriteFailureRetainsData)
{
	CFileAutoClose file(TestFile(), CFile::read);
	CFileArea area;
	area.StartWriteAt(file, 0, 23);
	std::memset(area.GetBuffer(), 0x5A, 23);
	ASSERT_RAISES(CIOFailureException, area.FlushAt(file, 0, 23));
	ASSERT_TRUE(area.GetBuffer() != nullptr);
	ASSERT_EQUALS(0x5A, static_cast<int>(area.GetBuffer()[0]));
}

TEST(FileArea, UninitializedAreaCannotReportACompletedWrite)
{
	CFileAutoClose file(TestFile(), CFile::read_write);
	CFileArea area;
	ASSERT_RAISES(CIOFailureException, area.FlushAt(file, 0, 23));
}

#if defined(AMULE_TEST_WRAP_MSYNC) && defined(MMAP_SUPPORTED)
TEST(FileArea, MappedSyncFailureThrowsAndCanBeRetried)
{
	CFileArea::SetMMapEnabled(true);
	CFileAutoClose file(TestFile(), CFile::read_write);
	CFileArea area;
	area.StartWriteAt(file, 0, 4096);
	std::memset(area.GetBuffer(), 0x5A, 4096);
	const auto previousCalls = syncCalls;
	failSync = true;
	ASSERT_RAISES(CIOFailureException, area.FlushAt(file, 0, 4096));
	ASSERT_EQUALS(previousCalls + 1, syncCalls);
	ASSERT_TRUE(area.GetBuffer() != nullptr);
	ASSERT_EQUALS(0x5A, static_cast<int>(area.GetBuffer()[0]));
	failSync = false;
	area.FlushAt(file, 0, 4096);
	ASSERT_EQUALS(previousCalls + 2, syncCalls);
	ASSERT_TRUE(area.GetBuffer() == nullptr);
	std::array<unsigned char, 4096> data{};
	file.ReadAt(data.data(), 0, data.size());
	ASSERT_TRUE(std::all_of(data.begin(), data.end(), [](unsigned char value) { return value == 0x5A; }));
}
#endif
