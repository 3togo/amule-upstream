// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later
#include <muleunit/test.h>
#include "UDPSourceRequestQueue.h"

using namespace muleunit;
DECLARE_SIMPLE(UDPSourceRequestQueue)

TEST(UDPSourceRequestQueue, NewFilesAreBatchedWithoutDelayingTheRegularPass)
{
	UDPSourceRequestQueue<int> queue;
	std::set<int> targets;
	queue.Queue(1, 0);
	queue.Queue(2, 0);
	ASSERT_TRUE(queue.Begin(0, false, targets));
	ASSERT_EQUALS(size_t{ 2 }, targets.size());
	queue.Queue(3, 1);
	ASSERT_FALSE(queue.Ready(59999));
	ASSERT_FALSE(queue.Begin(59999, false, targets));
	ASSERT_TRUE(queue.Begin(60000, false, targets));
	ASSERT_EQUALS(size_t{ 1 }, targets.size());
	ASSERT_TRUE(targets.count(3) == 1);
	queue.Queue(4, 60001);
	ASSERT_TRUE(queue.Begin(60001, true, targets));
	ASSERT_TRUE(targets.empty());
	ASSERT_FALSE(queue.Ready(120001));
}

TEST(UDPSourceRequestQueue, RepeatedResumesAndDeleteReaddsCannotEvadeCooldown)
{
	UDPSourceRequestQueue<int> queue;
	std::set<int> targets;
	queue.Requested(1, 100);
	queue.Queue(1, 101);
	queue.Remove(1);
	queue.Queue(1, 1299999);
	ASSERT_FALSE(queue.Ready(1299999));
	queue.Queue(1, 1300100);
	ASSERT_TRUE(queue.Begin(1300100, false, targets));
	ASSERT_TRUE(targets.count(1) == 1);
}

TEST(UDPSourceRequestQueue, FullPassQueriesSuppressRedundantPendingResumes)
{
	UDPSourceRequestQueue<int> queue;
	std::set<int> targets;
	ASSERT_TRUE(queue.Begin(100, true, targets));
	queue.Queue(1, 101);
	queue.Requested(1, 102);
	ASSERT_FALSE(queue.Ready(60100));
	queue.Queue(2, 103);
	ASSERT_TRUE(queue.Begin(60100, false, targets));
	ASSERT_TRUE(targets.count(1) == 0);
	ASSERT_TRUE(targets.count(2) == 1);
}

TEST(UDPSourceRequestQueue, InterruptedPassSurvivesQueriesToEarlierServers)
{
	UDPSourceRequestQueue<int> queue;
	std::set<int> targets;
	queue.Queue(1, 0);
	ASSERT_TRUE(queue.Begin(0, false, targets));
	queue.Requested(1, 1);
	queue.Interrupted(targets);
	ASSERT_FALSE(queue.Ready(59999));
	ASSERT_TRUE(queue.Begin(60000, false, targets));
	ASSERT_TRUE(targets.count(1) == 1);
}

TEST(UDPSourceRequestQueue, DeletedAndClearedDownloadsDoNotRemainPending)
{
	UDPSourceRequestQueue<int> queue;
	std::set<int> targets;
	queue.Queue(1, 0);
	queue.Remove(1);
	ASSERT_FALSE(queue.Begin(0, false, targets));
	queue.Queue(2, 0);
	queue.Clear();
	ASSERT_FALSE(queue.Ready(60000));
}
