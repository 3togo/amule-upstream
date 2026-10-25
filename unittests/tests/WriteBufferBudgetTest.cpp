// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later
#include <muleunit/test.h>
#include "WriteBufferBudget.h"
#include <atomic>
#include <cstdlib>
#include <limits>
#include <thread>
#include <vector>

using namespace muleunit;
DECLARE_SIMPLE(WriteBufferBudget)

TEST(WriteBufferBudget, OnlyReceivedFragmentsConsumeCapacity)
{
	WriteBufferBudget budget(100, 20);
	budget.Identify("a", "disk1");
	budget.Identify("alias", "disk1");
	budget.Identify("b", "disk2");
	// Selecting arbitrarily many network requests creates no leases.
	ASSERT_EQUALS(uint64_t{ 0 }, budget.Used());
	ASSERT_FALSE(budget.IsPressured("a"));
	auto fragment = budget.Charge("a", 15);
	ASSERT_TRUE(budget.IsPressured("alias"));
	ASSERT_FALSE(budget.IsPressured("b"));
	// Already requested data remains admissible above the threshold.
	auto inFlight = budget.Charge("alias", 10);
	ASSERT_EQUALS(uint64_t{ 25 }, budget.Used());
	fragment.reset();
	ASSERT_EQUALS(uint64_t{ 10 }, budget.Used());
	ASSERT_FALSE(budget.IsPressured("a"));
}

TEST(WriteBufferBudget, FailedFragmentKeepsOnlyItsOwnPayload)
{
	WriteBufferBudget budget(100, 20);
	auto written = budget.Charge("path", 12);
	auto failed = budget.Charge("path", 4);
	ASSERT_TRUE(budget.IsPressured("path"));
	written.reset();
	ASSERT_EQUALS(uint64_t{ 4 }, budget.Used());
	ASSERT_FALSE(budget.IsPressured("path"));
	ASSERT_TRUE(budget.Identify("path", "disk1"));
	ASSERT_FALSE(budget.Identify("path", "disk2"));
	failed.reset();
	ASSERT_TRUE(budget.Identify("path", "disk2"));
	ASSERT_EQUALS(uint64_t{ 0 }, budget.Used());
}

TEST(WriteBufferBudget, PressureFlushesSmallFilesAndClearsAfterDrain)
{
	WriteBufferBudget budget(100, 20);
	budget.Identify("a", "disk1");
	budget.Identify("b", "disk1");
	auto first = budget.Charge("a", 7);
	auto second = budget.Charge("b", 8);
	ASSERT_TRUE(budget.IsPressured("a"));
	first.reset();
	ASSERT_FALSE(budget.IsPressured("a"));
	second.reset();
	ASSERT_FALSE(budget.IsPressured("a"));
}

TEST(WriteBufferBudget, LocalPressureDoesNotFlushIndependentVolumes)
{
	WriteBufferBudget budget(100, 20);
	budget.Identify("hot", "disk1");
	budget.Identify("alias", "disk1");
	budget.Identify("healthy", "disk2");
	auto hot = budget.Charge("hot", 11);
	auto extra = budget.Charge("hot", 4);
	auto healthy = budget.Charge("healthy", 2);
	ASSERT_TRUE(budget.IsPressured("hot"));
	ASSERT_TRUE(budget.IsPressured("alias"));
	ASSERT_FALSE(budget.IsPressured("healthy"));
	extra.reset();
	ASSERT_TRUE(budget.IsPressured("hot")); // 55% retains the pressure latch
	hot.reset();
	ASSERT_FALSE(budget.IsPressured("hot"));
	ASSERT_FALSE(budget.IsPressured("healthy"));
}

TEST(WriteBufferBudget, UnknownPressureCountsOnlyPossibleAliasingDebt)
{
	WriteBufferBudget budget(100, 20);
	budget.Identify("hot", "disk1");
	budget.Identify("healthy", "disk2");
	auto hot = budget.Charge("hot", 3);
	auto healthy = budget.Charge("healthy", 2);
	auto unknown = budget.Charge("unknown", 12);
	ASSERT_TRUE(budget.IsPressured("hot"));
	ASSERT_TRUE(budget.IsPressured("unknown"));
	ASSERT_FALSE(budget.IsPressured("healthy"));
	unknown.reset();
	ASSERT_FALSE(budget.IsPressured("hot"));
	ASSERT_FALSE(budget.IsPressured("unknown"));
}

TEST(WriteBufferBudget, GlobalPressureFlushesAllVolumesWithHysteresis)
{
	WriteBufferBudget budget(100, 40);
	budget.Identify("a", "disk1");
	budget.Identify("b", "disk2");
	budget.Identify("c", "disk3");
	budget.Identify("idle", "disk4");
	auto a = budget.Charge("a", 25);
	auto b = budget.Charge("b", 25);
	auto c = budget.Charge("c", 25);
	ASSERT_TRUE(budget.IsPressured("idle"));
	c.reset(); // 50% clears global pressure
	ASSERT_FALSE(budget.IsPressured("idle"));
	ASSERT_FALSE(budget.IsPressured("a"));
}
