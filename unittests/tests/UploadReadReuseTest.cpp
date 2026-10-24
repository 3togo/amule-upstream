// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later
#include <muleunit/test.h>
#include "UploadReadReuse.h"
using namespace muleunit;
DECLARE_SIMPLE(UploadReadReuse)

TEST(UploadReadReuse, ExactAndPartialReuseCountCoveredBytesOnlyOnce)
{
	UploadReadReuse reuse;
	std::array<uint8_t, 16> hash{};
	reuse.Observe(hash.data(), 0, 100);
	reuse.Observe(hash.data(), 50, 150);
	reuse.Observe(hash.data(), 0, 100);
	auto stats = reuse.Take();
	ASSERT_EQUALS(uint64_t{ 3 }, stats.reads);
	ASSERT_EQUALS(uint64_t{ 300 }, stats.bytes);
	ASSERT_EQUALS(uint64_t{ 1 }, stats.exactReads);
	ASSERT_EQUALS(uint64_t{ 100 }, stats.exactBytes);
	ASSERT_EQUALS(uint64_t{ 2 }, stats.overlapReads);
	ASSERT_EQUALS(uint64_t{ 150 }, stats.overlapBytes);
	// Take resets the interval counters, retaining its bounded history.
	reuse.Observe(hash.data(), 75, 175);
	stats = reuse.Take();
	ASSERT_EQUALS(uint64_t{ 75 }, stats.overlapBytes);
	reuse.Observe(hash.data(), 150, 200);
	reuse.Take();
	reuse.Observe(hash.data(), 0, 200);
	stats = reuse.Take();
	ASSERT_EQUALS(uint64_t{ 200 }, stats.overlapBytes);
	ASSERT_EQUALS(uint64_t{ 0 }, stats.exactReads);
	hash[0] = 1;
	reuse.Observe(hash.data(), 0, 200);
	ASSERT_EQUALS(uint64_t{ 0 }, reuse.Take().overlapBytes);
}
TEST(UploadReadReuse, BoundedHistoryKeepsRecentlyRepeatedRangesAndReportsEviction)
{
	UploadReadReuse reuse;
	std::array<uint8_t, 16> hash{};
	for (size_t i = 0; i < UploadReadReuse::kHistory; ++i) {
		reuse.Observe(hash.data(), i * 1000, i * 1000 + 100);
	}
	reuse.Observe(hash.data(), 0, 100); // refresh the oldest entry
	reuse.Observe(hash.data(), 1000000, 1000100);
	ASSERT_EQUALS(uint64_t{ 1 }, reuse.Take().evictions);
	reuse.Observe(hash.data(), 1000, 1100); // the second entry was evicted
	auto stats = reuse.Take();
	ASSERT_EQUALS(uint64_t{ 0 }, stats.overlapReads);
	reuse.Reset();
	reuse.Observe(hash.data(), 0, 100);
	ASSERT_EQUALS(uint64_t{ 0 }, reuse.Take().exactReads);
}
TEST(UploadReadReuse, InvalidRangesDoNotCreateObservations)
{
	UploadReadReuse reuse;
	std::array<uint8_t, 16> hash{};
	reuse.Observe(nullptr, 0, 100);
	reuse.Observe(hash.data(), 100, 100);
	reuse.Observe(hash.data(), 101, 100);
	ASSERT_EQUALS(uint64_t{ 0 }, reuse.Take().reads);
}
