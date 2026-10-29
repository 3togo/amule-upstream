// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later
#include <muleunit/test.h>
#include "PartVerification.h"
#include <limits>
#include <vector>
using namespace muleunit;
DECLARE_SIMPLE(PartVerification)

TEST(PartVerification, RangesClipLastPartAndRejectOutOfBounds)
{
	const auto tail = PartVerificationRange(2, 25, 10);
	ASSERT_TRUE(tail.has_value());
	ASSERT_EQUALS(uint64_t{ 20 }, tail->offset);
	ASSERT_EQUALS(uint64_t{ 5 }, tail->length);
	ASSERT_FALSE(PartVerificationRange(3, 25, 10));
	ASSERT_FALSE(PartVerificationRange(1, 10, 10));
	ASSERT_FALSE(PartVerificationRange(0, 0, 10));
	ASSERT_FALSE(PartVerificationRange(0, 10, 0));
	const uint64_t maximum = std::numeric_limits<uint64_t>::max();
	ASSERT_FALSE(PartVerificationRange(maximum, maximum, 10));
	const auto last = PartVerificationRange(maximum - 1, maximum, 1);
	ASSERT_TRUE(last.has_value());
	ASSERT_EQUALS(uint64_t{ 1 }, last->length);
}

TEST(PartVerification, SkipsCleanIncompleteAndPendingParts)
{
	// First-share may proceed while other parts still have writes, but the
	// selected part itself must be complete on disk, not just in the gap map.
	const std::vector<bool> dirty{ false, true, true, true };
	const std::vector<bool> complete{ true, false, true, true };
	bool pending = true;
	const auto find = [&](uint64_t start) {
		return FindNextPartToVerify(
			35,
			10,
			start,
			[&](uint64_t part) { return dirty[part]; },
			[&](uint64_t part) { return complete[part]; },
			[&](const CPartVerificationRange &range) { return pending && range.part == 2; });
	};
	const auto first = find(0);
	ASSERT_TRUE(first.has_value());
	ASSERT_EQUALS(uint64_t{ 3 }, first->part);
	ASSERT_EQUALS(uint64_t{ 5 }, first->length);
	pending = false;
	const auto drained = find(0);
	ASSERT_TRUE(drained.has_value());
	ASSERT_EQUALS(uint64_t{ 2 }, drained->part);
	ASSERT_FALSE(find(4));
}

TEST(PartVerification, DoesNotInspectWriteStateForUnhashableParts)
{
	unsigned writesInspected = 0;
	const auto result = FindNextPartToVerify(
		30,
		10,
		0,
		[](uint64_t part) { return part != 0; },
		[](uint64_t part) { return part == 2; },
		[&](const CPartVerificationRange &) {
			++writesInspected;
			return true;
		});
	ASSERT_FALSE(result);
	ASSERT_EQUALS(1u, writesInspected);
}

TEST(PartVerification, LastByteAndEmptyFileTerminateWithoutOverflow)
{
	const uint64_t maximum = std::numeric_limits<uint64_t>::max();
	const auto dirty = [](uint64_t) { return true; };
	const auto complete = [](uint64_t) { return true; };
	const auto pending = [](const CPartVerificationRange &) { return true; };
	ASSERT_FALSE(FindNextPartToVerify(maximum, 1, maximum - 1, dirty, complete, pending));
	ASSERT_FALSE(FindNextPartToVerify(0, 1, 0, dirty, complete, pending));
	ASSERT_FALSE(FindNextPartToVerify(10, 0, 0, dirty, complete, pending));
}
