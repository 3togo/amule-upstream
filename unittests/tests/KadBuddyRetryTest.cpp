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
#include <kademlia/routing/Contact.h>
#include <Statistics.h>

using namespace muleunit;
using Kademlia::CBuddyRetry;

uint16 CStatistics::s_kadNodesCur = 0;

DECLARE_SIMPLE(KadBuddyRetry)

TEST(KadBuddyRetry, FailedEndpointIsSkippedUntilCooldownExpires)
{
	CBuddyRetry retry;
	const uint64_t now = 1000;
	ASSERT_TRUE(retry.CanRequest(1, 4662, now));
	retry.ConnectionStarted(1, 4662);
	retry.ConnectionFinished(true, now);
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
	retry.ConnectionStarted(1, 4662);
	retry.ConnectionFinished(true, now);
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
	retry.ConnectionStarted(2, 4662);
	retry.Connected();
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
	retry.ConnectionStarted(1, 4662);
	retry.ConnectionFinished(true, 0);
	retry.ConnectionStarted(2, 4662);
	retry.ConnectionFinished(true, 0);
	uint64_t now = 0;
	for (unsigned round = 0; round < 3; ++round) {
		ASSERT_TRUE(retry.CanStartSearch(now));
		retry.SearchStarted();
		ASSERT_TRUE(retry.FinishRound(now, false, false));
		now += CBuddyRetry::InitialSearchDelay << round;
	}
	retry.ConnectionStarted(1, 4662);
	retry.Connected();
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
		retry.ConnectionStarted(ip, 4662);
		retry.ConnectionFinished(true, CBuddyRetry::MaximumFailedCandidates - ip);
	}
	retry.ConnectionStarted(1000, 4662);
	retry.ConnectionFinished(true, CBuddyRetry::MaximumFailedCandidates);
	ASSERT_TRUE(retry.CanRequest(CBuddyRetry::MaximumFailedCandidates, 4662, 1000));
	ASSERT_FALSE(retry.CanRequest(1, 4662, 1000));
	ASSERT_FALSE(retry.CanRequest(1000, 4662, 1000));
}

TEST(KadBuddyRetry, ExpiredFailureCanBeRecordedAgain)
{
	CBuddyRetry retry;
	retry.ConnectionStarted(1, 4662);
	retry.ConnectionFinished(true, 0);
	const uint64_t now = CBuddyRetry::CandidateCooldown;
	retry.ConnectionStarted(2, 4662);
	retry.ConnectionFinished(true, now);
	ASSERT_TRUE(retry.CanRequest(1, 4662, now));
	retry.ConnectionStarted(1, 4662);
	retry.ConnectionFinished(true, now);
	ASSERT_FALSE(retry.CanRequest(1, 4662, now + 30000));
}

TEST(KadBuddyRetry, FirewallRecheckCannotRearmAnOldRound)
{
	CBuddyRetry retry;
	retry.ConnectionStarted(1, 4672);
	retry.ConnectionFinished(true, 0);
	retry.SearchStarted();
	retry.ConnectionStarted(2, 4672);
	// RecheckFirewalled clears the find-buddy trigger and cancels the old round.
	retry.ResetSearch();
	retry.ConnectionFinished(true, 1000);
	ASSERT_FALSE(retry.FinishRound(1000, false, false));
	ASSERT_FALSE(retry.CanRequest(1, 4672, 1000));
	ASSERT_TRUE(retry.CanRequest(2, 4672, 1000));
	// Once the existing firewall delay allows a new search, retry delay starts fresh.
	const uint64_t now = 5 * 60 * 1000;
	ASSERT_TRUE(retry.CanStartSearch(now));
	retry.SearchStarted();
	ASSERT_TRUE(retry.FinishRound(now, false, false));
	ASSERT_FALSE(retry.CanStartSearch(now + CBuddyRetry::InitialSearchDelay - 1));
	ASSERT_TRUE(retry.CanStartSearch(now + CBuddyRetry::InitialSearchDelay));
}

TEST(KadBuddyRetry, StopAndRestartClearPendingRoundsAndFailedDestinations)
{
	CBuddyRetry retry;
	retry.ConnectionStarted(1, 4672);
	retry.ConnectionFinished(true, 0);
	retry.SearchStarted();
	retry.ConnectionStarted(2, 4672);
	retry = {};
	retry.ConnectionFinished(true, 1);
	ASSERT_TRUE(retry.CanRequest(1, 4672, 1));
	ASSERT_TRUE(retry.CanRequest(2, 4672, 1));
	ASSERT_TRUE(retry.CanStartSearch(1));
	ASSERT_FALSE(retry.FinishRound(1, false, false));
}

TEST(KadBuddyRetry, RoutingAndBuddyResponseTcpPortsCannotBypassUdpCooldown)
{
	CBuddyRetry retry;
	const uint32_t ip = 0x12345678;
	const uint16_t udpPort = 4672;
	Kademlia::CContact contact(
		Kademlia::CUInt128(1u), ip, udpPort, 4662, 8, 0, false, Kademlia::CUInt128(0u));
	ASSERT_TRUE(retry.CanRequest(contact, 0));
	retry.ConnectionStarted(ip, udpPort);
	retry.ConnectionFinished(true, 0);
	ASSERT_FALSE(retry.CanRequest(contact, 30000));
	// The buddy response may announce a different TCP port from the routing table.
	// Both production admission paths use this same contact adapter.
	contact.SetTCPPort(16662);
	ASSERT_FALSE(retry.CanRequest(contact, 30000));
	ASSERT_TRUE(retry.CanRequest(contact, CBuddyRetry::CandidateCooldown));
	contact.SetUDPPort(14672);
	ASSERT_TRUE(retry.CanRequest(contact, 30000));
}

TEST(KadBuddyRetry, FailedHandshakeUsesTheOriginalRequestDestination)
{
	CBuddyRetry retry;
	const uint32_t ip = 0x12345678;
	Kademlia::CContact contact(
		Kademlia::CUInt128(1u), ip, 4672, 4662, 8, 0, false, Kademlia::CUInt128(0u));
	retry.ConnectionStarted(contact.GetIPAddress(), contact.GetUDPPort());
	// Hello parsing can update the peer before a malformed handshake is rejected.
	contact.SetUDPPort(14672);
	contact.SetTCPPort(16662);
	retry.ConnectionFinished(true, 1000);
	ASSERT_FALSE(retry.CanRequest(ip, 4672, 30000));
	ASSERT_TRUE(retry.CanRequest(contact, 30000));
	// Duplicate close notifications must not extend the original cooldown.
	retry.ConnectionFinished(true, 30000);
	ASSERT_TRUE(retry.CanRequest(ip, 4672, 1000 + CBuddyRetry::CandidateCooldown));
}

TEST(KadBuddyRetry, LocalCancellationDoesNotPenalizeTheCandidate)
{
	CBuddyRetry retry;
	retry.ConnectionStarted(1, 4672);
	retry.ConnectionFinished(false, 1000);
	ASSERT_TRUE(retry.CanRequest(1, 4672, 1000));
	// A later socket callback from the cancelled attempt has no saved destination.
	retry.ConnectionFinished(true, 2000);
	ASSERT_TRUE(retry.CanRequest(1, 4672, 2000));
}
