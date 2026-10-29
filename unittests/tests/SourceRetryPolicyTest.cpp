// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later
#include <muleunit/test.h>
#include "SourceRetryPolicy.h"
#include "RequestPacing.h"
using namespace muleunit;
using namespace SourceRetryPolicy;
DECLARE_SIMPLE(SourceRetryPolicy)

TEST(SourceRetryPolicy, KeepsProtocolReaskIntervalsAndStrictBoundary)
{
	const uint64_t reask = 29 * 60000;
	ASSERT_EQUALS(reask, Delay(Reason::Reask, reask));
	ASSERT_EQUALS(reask - 20000, Delay(Reason::UdpProbe, reask));
	ASSERT_EQUALS(reask * 2, Delay(Reason::NoNeededParts, reask));
	for (const auto reason : { Reason::Reask, Reason::UdpProbe, Reason::NoNeededParts }) {
		const uint64_t deadline = 1000 + Delay(reason, reask);
		ASSERT_FALSE(IsDue(deadline, 1000, reason, reask));
		ASSERT_TRUE(IsDue(deadline + 1, 1000, reason, reask));
		ASSERT_TRUE(IsDue(1000, 0, reason, reask));
		ASSERT_FALSE(IsDue(999, 1000, reason, reask));
	}
}
TEST(SourceRetryPolicy, KeepsGlobalAndPerFileDeadSourceExclusions)
{
	ASSERT_EQUALS(uint64_t{ 30 * 60000 }, DeadSourceDelay(true, false));
	ASSERT_EQUALS(uint64_t{ 45 * 60000 }, DeadSourceDelay(true, true));
	ASSERT_EQUALS(uint64_t{ 45 * 60000 }, DeadSourceDelay(false, false));
	ASSERT_EQUALS(uint64_t{ 60 * 60000 }, DeadSourceDelay(false, true));
	ASSERT_EQUALS(uint64_t{ 1000 + 60 * 60000 }, DeadSourceDeadline(1000, false, true));
}
TEST(SourceRetryPolicy, BoundsArithmeticForExtremeConfigurationValues)
{
	const uint64_t maximum = std::numeric_limits<uint64_t>::max();
	ASSERT_EQUALS(uint64_t{ 0 }, Delay(Reason::UdpProbe, 1));
	ASSERT_EQUALS(maximum, Delay(Reason::NoNeededParts, maximum));
	ASSERT_EQUALS(maximum, DeadSourceDeadline(maximum - 1, false, true));
	ASSERT_FALSE(IsDue(maximum, 1, Reason::NoNeededParts, maximum));
}
TEST(SourceRetryPolicy, PacedRetryDoesNotBypassAggressiveRequestGuard)
{
	const uint64_t reask = 29 * 60000;
	const uint64_t lastSent = reask * 2;
	const uint64_t lastAsked = PacedLastAskedTime(lastSent + 1000, lastSent, reask);
	ASSERT_FALSE(IsDue(lastSent + MIN_REQUESTTIME, lastAsked, Reason::Reask, reask));
	ASSERT_TRUE(IsDue(lastSent + MIN_REQUESTTIME + 1, lastAsked, Reason::Reask, reask));
}
