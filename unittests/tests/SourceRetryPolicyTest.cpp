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
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
//

#include <muleunit/test.h>
#include "SourceRetryPolicy.h"
#include "RequestPacing.h"
using namespace muleunit;
using namespace SourceRetryPolicy;
DECLARE_SIMPLE(SourceRetryPolicy)

TEST(SourceRetryPolicy, KeepsProtocolReaskIntervalsAndStrictBoundary)
{
	for (const uint64_t reask : { 15 * 60000, 29 * 60000, 60 * 60000 }) {
		for (const uint64_t delay : { reask, reask - 20000, reask * 2 }) {
			const uint64_t deadline = 1000 + delay;
			ASSERT_FALSE(IsDue(deadline - 1, 1000, delay));
			ASSERT_FALSE(IsDue(deadline, 1000, delay));
			ASSERT_TRUE(IsDue(deadline + 1, 1000, delay));
			ASSERT_TRUE(IsDue(1000, 0, delay));
		}
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
TEST(SourceRetryPolicy, PacedRetryDoesNotBypassAggressiveRequestGuard)
{
	const uint64_t reask = 29 * 60000;
	const uint64_t lastSent = reask * 2;
	const uint64_t lastAsked = PacedLastAskedTime(lastSent + 1000, lastSent, reask);
	ASSERT_FALSE(IsDue(lastSent + MIN_REQUESTTIME, lastAsked, reask));
	ASSERT_TRUE(IsDue(lastSent + MIN_REQUESTTIME + 1, lastAsked, reask));
}
