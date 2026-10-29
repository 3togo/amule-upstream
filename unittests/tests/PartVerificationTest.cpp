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
			4,
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
		3,
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
	ASSERT_FALSE(FindNextPartToVerify(maximum, 1, maximum, maximum - 1, dirty, complete, pending));
	ASSERT_FALSE(FindNextPartToVerify(0, 1, 0, 0, dirty, complete, pending));
	ASSERT_FALSE(FindNextPartToVerify(10, 0, 0, 0, dirty, complete, pending));
}

TEST(PartVerification, ExactPartMultiplesNeverInspectAnEmptySyntheticPart)
{
	for (uint64_t size : { uint64_t{ 10 }, uint64_t{ 20 }, uint64_t{ 21 } }) {
		const uint64_t count = (size - 1) / 10 + 1;
		uint64_t inspected = 0;
		const auto result = FindNextPartToVerify(
			size,
			10,
			count,
			0,
			[&](uint64_t part) {
				ASSERT_TRUE(part < count);
				++inspected;
				return false;
			},
			[](uint64_t) { return true; },
			[](const CPartVerificationRange &) { return false; });
		ASSERT_FALSE(result);
		ASSERT_EQUALS(count, inspected);
	}
}

TEST(PartVerification, EligibilityMatchesAnIndependentStateMaskOracle)
{
	// Every combination of dirty, complete and pending-write state for four
	// parts. The final part has only one byte; all callbacks must stay in bounds.
	for (unsigned dirty = 0; dirty < 16; ++dirty) {
		for (unsigned complete = 0; complete < 16; ++complete) {
			for (unsigned pending = 0; pending < 16; ++pending) {
				const unsigned eligible = dirty & complete & ~pending & 15;
				unsigned expected = 0;
				while (expected < 4 && !(eligible & (1u << expected))) {
					++expected;
				}
				const auto result = FindNextPartToVerify(
					31,
					10,
					4,
					0,
					[&](uint64_t part) {
						ASSERT_TRUE(part < 4);
						return dirty & (1u << part);
					},
					[&](uint64_t part) {
						ASSERT_TRUE(part < 4);
						return complete & (1u << part);
					},
					[&](const CPartVerificationRange &range) {
						return pending & (1u << range.part);
					});
				ASSERT_EQUALS(expected < 4, result.has_value());
				if (result) {
					ASSERT_EQUALS(expected, result->part);
					ASSERT_EQUALS(uint64_t{ expected * 10 }, result->offset);
					ASSERT_EQUALS(expected == 3 ? uint64_t{ 1 } : uint64_t{ 10 },
						result->length);
				}
			}
		}
	}
}

TEST(PartVerification, InconsistentMetadataCannotIndexPastTrackedState)
{
	const std::vector<bool> dirty{ false, false };
	uint64_t inspected = 0;
	const auto result = FindNextPartToVerify(
		std::numeric_limits<uint64_t>::max(),
		10,
		dirty.size(),
		0,
		[&](uint64_t part) {
			ASSERT_TRUE(part < dirty.size());
			++inspected;
			return dirty[part];
		},
		[](uint64_t) { return true; },
		[](const CPartVerificationRange &) { return false; });
	ASSERT_FALSE(result);
	ASSERT_EQUALS(uint64_t{ 2 }, inspected);
	const auto untouched = [&](uint64_t) {
		ASSERT_TRUE(false);
		return false;
	};
	ASSERT_FALSE(FindNextPartToVerify(
		100, 10, 0, 0, untouched, untouched, [](const CPartVerificationRange &) { return false; }));
	ASSERT_FALSE(FindNextPartToVerify(
		100, 10, 2, 2, untouched, untouched, [](const CPartVerificationRange &) { return false; }));
}
