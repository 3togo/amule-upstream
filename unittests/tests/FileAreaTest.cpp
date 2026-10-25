// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"
#include <muleunit/test.h>
#include "FileArea.h"
#include "FileAutoClose.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstring>
#include "WriteBufferBudget.h"
#include "BufferedWriteBlock.h"
#ifdef AMULE_TEST_WRAP_MSYNC
#include <unistd.h>
static bool shortScalarWrite = false;
extern "C" ssize_t __real_write(int fd, const void *data, size_t size);
extern "C" ssize_t __wrap_write(int fd, const void *data, size_t size)
{
	return __real_write(fd, data, shortScalarWrite && size > 1 ? size - 1 : size);
}
#endif

std::atomic<uint32> TheTime{ 0 };

#if defined(AMULE_TEST_WRAP_MSYNC) && defined(MMAP_SUPPORTED)
#include <unistd.h>

namespace
{
bool failSync = false;
unsigned syncCalls = 0;
int truncateOnFailure = -1;
} // namespace
extern "C" int __real_msync(void *address, size_t length, int flags);
extern "C" int __wrap_msync(void *address, size_t length, int flags)
{
	++syncCalls;
	if (failSync) {
		// Model writeback that cleaned the pages before reporting an error. A
		// later msync alone must not be enough to complete the pending write.
		if (__real_msync(address, length, flags) != 0) {
			return -1;
		}
		// Model a device whose pages are gone: reading the mapping now raises SIGBUS.
		if (truncateOnFailure >= 0 && ftruncate(truncateOnFailure, 0) != 0) {
			return -1;
		}
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
#ifdef AMULE_TEST_WRAP_MSYNC
	shortScalarWrite = false;
#endif
#if defined(AMULE_TEST_WRAP_MSYNC) && defined(MMAP_SUPPORTED)
	failSync = false;
	truncateOnFailure = -1;
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
TEST(FileArea, MappedWriteRoundTrip)
{
	CFileArea::SetMMapEnabled(true);
	CFileAutoClose file(TestFile(), CFile::read_write);
	CFileArea area;
	area.StartWriteAt(file, 17, 5000);
	std::memset(area.GetBuffer(), 0x5A, 5000);
	const auto previousCalls = syncCalls;
	area.FlushAt(file, 17, 5000);
	ASSERT_EQUALS(previousCalls + 1, syncCalls);
	ASSERT_TRUE(area.GetBuffer() == nullptr);
	ASSERT_TRUE(file.Release(true));
	std::array<unsigned char, 8192> data{};
	file.ReadAt(data.data(), 0, data.size());
	for (size_t i = 0; i < data.size(); ++i) {
		ASSERT_EQUALS(i >= 17 && i < 5017 ? 0x5A : 0, static_cast<int>(data[i]));
	}
}

TEST(FileArea, MappedSyncFailureRewritesData)
{
	CFileArea::SetMMapEnabled(true);
	CFileAutoClose file(TestFile(), CFile::read_write);
	CFileArea area;
	area.StartWriteAt(file, 17, 5000);
	std::memset(area.GetBuffer(), 0x5A, 5000);
	const auto previousCalls = syncCalls;
	failSync = true;
	ASSERT_RAISES(CIOFailureException, area.FlushAt(file, 17, 5000));
	ASSERT_EQUALS(previousCalls + 1, syncCalls);
	ASSERT_TRUE(area.GetBuffer() != nullptr);
	ASSERT_TRUE(std::all_of(area.GetBuffer(), area.GetBuffer() + 5000, [](unsigned char value) {
		return value == 0x5A;
	}));
	ASSERT_TRUE(file.Release(true));

	// Replace the file contents after the failure. A retained mapping or a
	// successful no-op sync would now lose the original pending bytes.
	std::array<unsigned char, 8192> data{};
	file.WriteAt(data.data(), 0, data.size());
	area.FlushAt(file, 17, 5000);
	ASSERT_EQUALS(previousCalls + 1, syncCalls);
	ASSERT_TRUE(area.GetBuffer() == nullptr);
	file.ReadAt(data.data(), 0, data.size());
	for (size_t i = 0; i < data.size(); ++i) {
		ASSERT_EQUALS(i >= 17 && i < 5017 ? 0x5A : 0, static_cast<int>(data[i]));
	}
}

TEST(FileArea, MappedSyncFailureRetainsDataAcrossFailedBufferedRetry)
{
	CFileArea::SetMMapEnabled(true);
	CFileAutoClose file(TestFile(), CFile::read_write);
	CFileArea area;
	area.StartWriteAt(file, 17, 5000);
	std::memset(area.GetBuffer(), 0x5A, 5000);
	const auto previousCalls = syncCalls;
	failSync = true;
	ASSERT_RAISES(CIOFailureException, area.FlushAt(file, 17, 5000));
	ASSERT_EQUALS(previousCalls + 1, syncCalls);
	CFileAutoClose readOnlyFile(TestFile(), CFile::read);
	ASSERT_RAISES(CIOFailureException, area.FlushAt(readOnlyFile, 17, 5000));
	ASSERT_EQUALS(previousCalls + 1, syncCalls);
	ASSERT_TRUE(area.GetBuffer() != nullptr);
	ASSERT_TRUE(std::all_of(area.GetBuffer(), area.GetBuffer() + 5000, [](unsigned char value) {
		return value == 0x5A;
	}));
	area.FlushAt(file, 17, 5000);
	ASSERT_EQUALS(previousCalls + 1, syncCalls);
	ASSERT_TRUE(area.GetBuffer() == nullptr);
	std::array<unsigned char, 5000> data{};
	file.ReadAt(data.data(), 17, data.size());
	ASSERT_TRUE(std::all_of(data.begin(), data.end(), [](unsigned char value) { return value == 0x5A; }));
}

TEST(FileArea, MappedSyncFailureSurvivesLostPages)
{
	CFileArea::SetMMapEnabled(true);
	CFileAutoClose file(TestFile(), CFile::read_write);
	{
		CFileArea area;
		area.StartWriteAt(file, 17, 5000);
		std::memset(area.GetBuffer(), 0x5A, 5000);
		failSync = true;
		truncateOnFailure = file.fd();
		ASSERT_RAISES(CIOFailureException, area.FlushAt(file, 17, 5000));
		truncateOnFailure = -1;
		ASSERT_TRUE(area.GetBuffer() != nullptr);
		ASSERT_EQUALS(0, static_cast<int>(area.GetBuffer()[0]));
	} // ~CFileArea must not rethrow the SIGBUS from the copy
}
#endif

TEST(FileArea, TinyFragmentsShareOnePayloadAndTwoImmutableValiditySnapshots)
{
	WriteBufferBudget budget;
	auto credit = budget.ReserveRequest("block", 4096, "disk");
	{
		BufferedWriteBlock first(17, 4112, credit), second(17, 4112, credit);
		ASSERT_TRUE(first.area.GetBuffer() == second.area.GetBuffer());
		ASSERT_RAISES(CIOFailureException, BufferedWriteBlock(17, 4112, credit));
		const uint8_t a = 0x31, b = 0x42;
		for (size_t i = 0; i < 4096; ++i) {
			(i % 2 == 0 ? first : second).Append(i % 2 == 0 ? &a : &b, 17 + i, 1);
		}
		ASSERT_EQUALS(uint32{ 2048 }, first.Bytes());
		ASSERT_EQUALS(uint32{ 2048 }, second.Bytes());
		ASSERT_EQUALS(uint64_t{ 2 }, budget.Fragments());
		credit.reset(); // cancellation cannot release live write payload
		ASSERT_EQUALS(uint64_t{ 4096 }, budget.Used());
		CFileAutoClose file(TestFile(), CFile::read_write);
#ifdef AMULE_TEST_WRAP_MSYNC
		shortScalarWrite = true;
		ASSERT_RAISES(CIOFailureException, first.Flush(file));
		ASSERT_TRUE(first.area.GetBuffer() != nullptr);
		ASSERT_EQUALS(uint32{ 2048 }, first.Bytes());
		shortScalarWrite = false;
#endif
		first.Flush(file);
		ASSERT_TRUE(first.area.GetBuffer() == nullptr);
		std::array<uint8_t, 4096> data{};
		file.ReadAt(data.data(), 17, data.size());
		for (size_t i = 0; i < data.size(); ++i) {
			ASSERT_EQUALS(i % 2 == 0 ? 0x31 : 0, static_cast<int>(data[i]));
		}
		// The first snapshot must not publish bytes received by the second,
		// even though they share the payload allocation. The later merge must
		// also preserve the first snapshot's previously written bytes.
		second.Flush(file);
		file.ReadAt(data.data(), 17, data.size());
		for (size_t i = 0; i < data.size(); ++i) {
			ASSERT_EQUALS(i % 2 == 0 ? 0x31 : 0x42, static_cast<int>(data[i]));
		}
	}
	ASSERT_EQUALS(uint64_t{ 0 }, budget.Used());
	ASSERT_EQUALS(uint64_t{ 0 }, budget.Fragments());
	ASSERT_EQUALS(size_t{ 0 }, budget.Accounts());
}
TEST(FileArea, SparseAssemblyPreservesExistingFileDataAndUninitializedTailStaysZero)
{
	WriteBufferBudget budget;
	CFileAutoClose file(TestFile(), CFile::read_write);
	std::array<uint8_t, 8192> old;
	old.fill(0x7A);
	file.WriteAt(old.data(), 0, old.size());
	auto credit = budget.ReserveRequest("block", 4096, "disk");
	BufferedWriteBlock partial(7000, 11095, credit);
	const uint8_t received = 0x31;
	partial.Append(&received, 7017, 1);
	partial.Append(&received, 11095, 1);
	partial.Flush(file);
	std::array<uint8_t, 4096> data{};
	file.ReadAt(data.data(), 7000, data.size());
	for (size_t i = 0; i < data.size(); ++i) {
		ASSERT_EQUALS(i == 17 || i == data.size() - 1 ? 0x31 : (i < 1192 ? 0x7A : 0),
			static_cast<int>(data[i]));
	}
	ASSERT_EQUALS(uint32{ 2 }, partial.Bytes());
}
