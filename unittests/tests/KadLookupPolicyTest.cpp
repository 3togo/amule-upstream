// This file is part of the aMule Project.
// Copyright (c) 2003-2026 aMule Team ( https://amule-org.github.io )
// Licensed under the GNU General Public License, version 2 or later.
#include <muleunit/test.h>
#include <kademlia/utils/LookupPolicy.h>

using namespace muleunit;
using namespace Kademlia;
DECLARE_SIMPLE(KadLookupPolicy)
namespace
{
struct Policy
{
	SKadConstraints settings;
	CLookupBudget budget;
	Policy()
	: budget(settings)
	{
		settings.Resolve(100, 30);
	}
};
} // namespace

TEST(KadLookupPolicy, DiversitySpansRepliesAndCountsSeeds)
{
	Policy f;
	CLookupDiversity peers(f.settings);
	peers.AddSeed(0x01020301);
	ASSERT_FALSE(peers.Admit(0x01020301, false));
	ASSERT_TRUE(peers.Admit(0x01020302, false));
	// This referral can arrive in an entirely different response.
	ASSERT_FALSE(peers.Admit(0x01020303, false));
	ASSERT_TRUE(peers.Admit(0x01020401, false));
	ASSERT_FALSE(peers.Admit(0x01020401, false));
}

TEST(KadLookupPolicy, LanAllowsConcentratedSubnetsButNotDuplicateAddresses)
{
	Policy f;
	CLookupDiversity peers(f.settings);
	for (uint32_t ip = 0xc0a80101; ip < 0xc0a801ff; ++ip) {
		ASSERT_TRUE(peers.Admit(ip, true));
		ASSERT_FALSE(peers.Admit(ip, true));
	}
}

TEST(KadLookupPolicy, ReplyFromOnePeerDoesNotMoveAnotherDeadline)
{
	Policy f;
	CLookupRequests requests(f.budget, f.settings);
	requests.Sent({ 1, 1 }, 0, 3000, f.settings.lookupConcurrency);
	requests.Sent({ 2, 2 }, 1000, 3000, f.settings.lookupConcurrency);
	ASSERT_FALSE(requests.CanSend(2999, 2));
	uint32_t elapsed = 0;
	ASSERT_TRUE(requests.Complete({ 2, 2 }, 2999, elapsed));
	ASSERT_EQUALS(uint32_t(1999), elapsed);
	requests.Sent({ 3, 3 }, 2999, 3000, f.settings.lookupConcurrency);
	ASSERT_FALSE(requests.CanSend(2999, 2));
	ASSERT_TRUE(requests.CanSend(3000, 2));
	// A soft timeout preserves the original clock and late-answer sample.
	ASSERT_TRUE(requests.Complete({ 1, 1 }, 4000, elapsed));
	ASSERT_EQUALS(uint32_t(4000), elapsed);
	ASSERT_FALSE(requests.Complete({ 1, 1 }, 4001, elapsed));
}

TEST(KadLookupPolicy, DeadlineIsFixedAndEndpointMustMatch)
{
	Policy f;
	CLookupRequests requests(f.budget, f.settings);
	requests.Sent({ 1, 2 }, 100, 3000, f.settings.lookupConcurrency);
	requests.Sent({ 1, 2 }, 2000, 10000, f.settings.lookupConcurrency);
	ASSERT_FALSE(requests.CanSend(3099, 1));
	ASSERT_TRUE(requests.CanSend(3100, 1));
	uint32_t elapsed = 0;
	ASSERT_FALSE(requests.Complete({ 1, 3 }, 3200, elapsed));
	ASSERT_TRUE(requests.Contains({ 1, 2 }));
	ASSERT_TRUE(requests.Complete({ 1, 2 }, 3200, elapsed));
	ASSERT_EQUALS(uint32_t(3100), elapsed);
}

TEST(KadLookupPolicy, LateRecordsAreBoundedAndPrunedAtTheirOwnDeadlines)
{
	Policy f;
	CLookupRequests requests(f.budget, f.settings);
	for (uint32_t ip = 0; ip < f.settings.lookupPending; ++ip) {
		ASSERT_TRUE(requests.CanSend(ip * 100, 1));
		requests.Sent({ ip, 1 }, ip * 100, 100, f.settings.lookupConcurrency);
	}
	ASSERT_FALSE(requests.CanSend(10000, 1));
	requests.Prune(10099);
	ASSERT_FALSE(requests.CanSend(10099, 1));
	requests.Prune(10100);
	ASSERT_FALSE(requests.Contains({ 0, 1 }));
	ASSERT_TRUE(requests.CanSend(10100, 1));
	uint32_t elapsed = 0;
	ASSERT_FALSE(requests.Complete({ 1, 1 }, 10200, elapsed));
	requests.Prune(20000);
	ASSERT_TRUE(requests.Empty());
}

TEST(KadLookupPolicy, CustomDiversityMatchesConfiguredRoutingLimits)
{
	Policy f;
	f.settings.subnetPrefix = 16;
	f.settings.contactsPerIP = 2;
	f.settings.contactsPerSubnet = 3;
	CLookupDiversity peers(f.settings);
	peers.AddSeed(0x01020001);
	peers.AddSeed(0x01020001);
	ASSERT_FALSE(peers.Admit(0x01020001, false));
	ASSERT_TRUE(peers.Admit(0x01020101, false));
	ASSERT_FALSE(peers.Admit(0x01020201, false));
	ASSERT_TRUE(peers.Admit(0x01020201, true));
}

TEST(KadLookupPolicy, ActiveBudgetIsSharedAcrossSearches)
{
	Policy f;
	f.settings.globalLookupActive = 2;
	CLookupRequests first(f.budget, f.settings), second(f.budget, f.settings);
	ASSERT_TRUE(first.Sent({ 1, 1 }, 0, 100, 5));
	ASSERT_TRUE(second.Sent({ 2, 2 }, 0, 100, 5));
	ASSERT_FALSE(first.Sent({ 3, 3 }, 0, 100, 5));
	ASSERT_FALSE(second.CanSend(99, 5));
	uint32_t elapsed = 0;
	ASSERT_FALSE(first.Complete({ 1, 2 }, 1, elapsed));
	ASSERT_FALSE(second.CanSend(1, 5));
	ASSERT_TRUE(first.Complete({ 1, 1 }, 1, elapsed));
	ASSERT_TRUE(second.Sent({ 3, 3 }, 1, 100, 5));
	second.Cancel({ 3, 3 });
	ASSERT_TRUE(first.CanSend(1, 5));
}

TEST(KadLookupPolicy, AggregateBudgetIncludesLateRecordsAndExpiresIdleOwners)
{
	Policy f;
	f.settings.globalLookupActive = 1;
	f.settings.globalLookupPending = 2;
	f.settings.sampleGraceMs = 100;
	CLookupRequests first(f.budget, f.settings), second(f.budget, f.settings),
		third(f.budget, f.settings);
	ASSERT_TRUE(first.Sent({ 1, 1 }, 0, 10, 5));
	ASSERT_FALSE(second.Sent({ 2, 2 }, 9, 10, 5));
	ASSERT_TRUE(second.Sent({ 2, 2 }, 10, 10, 5));
	ASSERT_FALSE(third.Sent({ 3, 3 }, 20, 10, 5));
	// No timer on the first search is needed to reclaim its aggregate lease.
	ASSERT_TRUE(third.Sent({ 3, 3 }, 110, 10, 5));
	uint32_t elapsed = 0;
	ASSERT_FALSE(first.Complete({ 1, 1 }, 111, elapsed));
	ASSERT_TRUE(second.Complete({ 2, 2 }, 111, elapsed));
	ASSERT_EQUALS(uint32_t(101), elapsed);
}

TEST(KadLookupPolicy, StoppingSearchReturnsItsReservations)
{
	Policy f;
	f.settings.globalLookupActive = 1;
	CLookupRequests second(f.budget, f.settings);
	{
		CLookupRequests first(f.budget, f.settings);
		ASSERT_TRUE(first.Sent({ 1, 1 }, 0, 100, 5));
		ASSERT_FALSE(second.Sent({ 2, 2 }, 0, 100, 5));
	}
	ASSERT_TRUE(second.Sent({ 2, 2 }, 0, 100, 5));
	second.Cancel({ 2, 2 });
	ASSERT_TRUE(second.Sent({ 2, 2 }, 0, 100, 5));
}

TEST(KadLookupPolicy, ClearingAStoppedSearchImmediatelyReturnsItsBudget)
{
	Policy f;
	f.settings.globalLookupPending = 1;
	CLookupRequests first(f.budget, f.settings), second(f.budget, f.settings);
	ASSERT_TRUE(first.Sent({ 1, 1 }, 0, 100, 5));
	ASSERT_FALSE(second.CanSend(100, 5));
	first.Clear();
	ASSERT_TRUE(first.Empty());
	ASSERT_TRUE(second.Sent({ 2, 2 }, 100, 100, 5));
}
