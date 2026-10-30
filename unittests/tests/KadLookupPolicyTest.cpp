// This file is part of the aMule Project.
// Copyright (c) 2003-2026 aMule Team ( https://amule-org.github.io )
// Licensed under the GNU General Public License, version 2 or later.
#include <muleunit/test.h>
#include <kademlia/utils/LookupPolicy.h>

using namespace muleunit;
using namespace Kademlia;
DECLARE_SIMPLE(KadLookupPolicy)

TEST(KadLookupPolicy, DiversitySpansRepliesAndCountsSeeds)
{
	CLookupDiversity peers;
	peers.AddSeed(0x01020301);
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
	CLookupDiversity peers;
	for (uint32_t ip = 0xc0a80101; ip < 0xc0a801ff; ++ip) {
		ASSERT_TRUE(peers.Admit(ip, true));
		ASSERT_FALSE(peers.Admit(ip, true));
	}
}

TEST(KadLookupPolicy, ReplyFromOnePeerDoesNotMoveAnotherDeadline)
{
	CLookupRequests requests;
	requests.Sent({ 1, 1 }, 0, 3000);
	requests.Sent({ 2, 2 }, 1000, 3000);
	ASSERT_FALSE(requests.CanSend(2999, 2));
	uint32_t elapsed = 0;
	ASSERT_TRUE(requests.Complete({ 2, 2 }, 2999, elapsed));
	ASSERT_EQUALS(uint32_t(1999), elapsed);
	requests.Sent({ 3, 3 }, 2999, 3000);
	ASSERT_FALSE(requests.CanSend(2999, 2));
	ASSERT_TRUE(requests.CanSend(3000, 2));
	// A soft timeout preserves the original clock and late-answer sample.
	ASSERT_TRUE(requests.Complete({ 1, 1 }, 4000, elapsed));
	ASSERT_EQUALS(uint32_t(4000), elapsed);
	ASSERT_FALSE(requests.Complete({ 1, 1 }, 4001, elapsed));
}

TEST(KadLookupPolicy, DeadlineIsFixedAndEndpointMustMatch)
{
	CLookupRequests requests;
	requests.Sent({ 1, 2 }, 100, 3000);
	requests.Sent({ 1, 2 }, 2000, 10000);
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
	CLookupRequests requests;
	for (uint32_t ip = 0; ip < CLookupRequests::MAX_PENDING; ++ip) {
		ASSERT_TRUE(requests.CanSend(ip * 100, 1));
		requests.Sent({ ip, 1 }, ip * 100, 100);
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
