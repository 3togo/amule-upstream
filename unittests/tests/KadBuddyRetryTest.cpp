//
// This file is part of the aMule Project.
//
// Copyright (c) 2003-2026 aMule Team ( https://amule-org.github.io )
//
// Any parts of this program derived from the xMule, lMule or eMule project,
// or contributed by third-party developers are copyrighted by their
// respective authors.
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

#include <muleunit/test.h>
#include <KadBuddyRetry.h>

using namespace muleunit;
using Kademlia::CBuddyRetry;

DECLARE_SIMPLE(KadBuddyRetry)

TEST(KadBuddyRetry, FailedEndpointIsSkippedUntilCooldownExpires)
{
	CBuddyRetry retry;
	const uint64_t now = 1000;
	ASSERT_TRUE(retry.CanRequest(1, 4662, now));
	retry.ConnectionFailed(1, 4662, now);
	ASSERT_FALSE(retry.CanRequest(1, 4662, now + 30 * 1000));
	ASSERT_FALSE(retry.CanRequest(1, 4662, now + CBuddyRetry::CandidateCooldown - 1));
	ASSERT_TRUE(retry.CanRequest(1, 4662, now + CBuddyRetry::CandidateCooldown));
	ASSERT_TRUE(retry.CanRequest(2, 4662, now));
	ASSERT_TRUE(retry.CanRequest(1, 4663, now));
}

TEST(KadBuddyRetry, RepeatedThirtySecondRoundsDoNotForgetFailedCandidate)
{
	CBuddyRetry retry;
	uint64_t now = 0;
	retry.SearchStarted();
	retry.ConnectionFailed(1, 4662, now);
	ASSERT_FALSE(retry.FinishRound(now, true, false));
	now = 30 * 1000;
	ASSERT_TRUE(retry.FinishRound(now, false, false));
	ASSERT_FALSE(retry.CanStartSearch(now));
	now += CBuddyRetry::InitialSearchDelay;
	ASSERT_TRUE(retry.CanStartSearch(now));
	retry.SearchStarted();
	// Rediscovery of the UDP-responsive node does not reset its TCP cooldown.
	ASSERT_FALSE(retry.CanRequest(1, 4662, now));
	ASSERT_TRUE(retry.CanRequest(2, 4662, now));
	retry.Connected(2, 4662);
	ASSERT_FALSE(retry.FinishRound(now, false, false));
	ASSERT_FALSE(retry.CanRequest(1, 4662, now));
}

TEST(KadBuddyRetry, SearchWaitsForLookupAndLastTcpAttempt)
{
	CBuddyRetry retry;
	retry.SearchStarted();
	ASSERT_FALSE(retry.CanStartSearch(0));
	ASSERT_FALSE(retry.FinishRound(0, true, false));
	ASSERT_FALSE(retry.FinishRound(100, false, true));
	ASSERT_TRUE(retry.FinishRound(200, false, false));
	ASSERT_FALSE(retry.FinishRound(300, false, false));
	ASSERT_FALSE(retry.CanStartSearch(200 + CBuddyRetry::InitialSearchDelay - 1));
	ASSERT_TRUE(retry.CanStartSearch(200 + CBuddyRetry::InitialSearchDelay));
}

TEST(KadBuddyRetry, UnsuccessfulRoundsBackOffUpToTwentyMinutes)
{
	CBuddyRetry retry;
	uint64_t now = uint64_t(1) << 33;
	const uint64_t delays[] = { 60000, 120000, 240000, 480000, 960000, 1200000, 1200000 };
	for (const uint64_t delay : delays) {
		ASSERT_TRUE(retry.CanStartSearch(now));
		retry.SearchStarted();
		ASSERT_TRUE(retry.FinishRound(now, false, false));
		ASSERT_FALSE(retry.CanStartSearch(now + delay - 1));
		now += delay;
		ASSERT_TRUE(retry.CanStartSearch(now));
	}
}

TEST(KadBuddyRetry, ConnectedBuddyResetsSearchDelayAndOnlyItsOwnFailure)
{
	CBuddyRetry retry;
	retry.ConnectionFailed(1, 4662, 0);
	retry.ConnectionFailed(2, 4662, 0);
	uint64_t now = 0;
	for (unsigned round = 0; round < 3; ++round) {
		ASSERT_TRUE(retry.CanStartSearch(now));
		retry.SearchStarted();
		ASSERT_TRUE(retry.FinishRound(now, false, false));
		now += CBuddyRetry::InitialSearchDelay << round;
	}
	retry.Connected(1, 4662);
	ASSERT_TRUE(retry.CanRequest(1, 4662, now));
	ASSERT_FALSE(retry.CanRequest(2, 4662, now));
	ASSERT_TRUE(retry.CanStartSearch(now));
	retry.SearchStarted();
	ASSERT_TRUE(retry.FinishRound(now, false, false));
	ASSERT_TRUE(retry.CanStartSearch(now + CBuddyRetry::InitialSearchDelay));
}

TEST(KadBuddyRetry, FailureCacheIsBoundedAndEvictsEarliestExpiry)
{
	CBuddyRetry retry;
	// Insert in reverse IP order: the oldest failure must be evicted, not the lowest IP.
	for (uint32_t ip = CBuddyRetry::MaximumFailedCandidates; ip > 0; --ip) {
		retry.ConnectionFailed(ip, 4662, CBuddyRetry::MaximumFailedCandidates - ip);
	}
	retry.ConnectionFailed(1000, 4662, CBuddyRetry::MaximumFailedCandidates);
	ASSERT_TRUE(retry.CanRequest(CBuddyRetry::MaximumFailedCandidates, 4662, 1000));
	ASSERT_FALSE(retry.CanRequest(1, 4662, 1000));
	ASSERT_FALSE(retry.CanRequest(1000, 4662, 1000));
}

TEST(KadBuddyRetry, ExpiredFailureCanBeRecordedAgain)
{
	CBuddyRetry retry;
	retry.ConnectionFailed(1, 4662, 0);
	const uint64_t now = CBuddyRetry::CandidateCooldown;
	retry.ConnectionFailed(2, 4662, now);
	ASSERT_TRUE(retry.CanRequest(1, 4662, now));
	retry.ConnectionFailed(1, 4662, now);
	ASSERT_FALSE(retry.CanRequest(1, 4662, now + 30000));
}
