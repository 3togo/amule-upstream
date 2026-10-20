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

#include "DisplayRateCounter.h"
#include <muleunit/test.h>

using namespace muleunit;

DECLARE_SIMPLE(DisplayRateCounterTest)

TEST(DisplayRateCounterTest, StartupIncludesIdleTime)
{
	CDisplayRateCounter rate;
	ASSERT_EQUALS(uint64_t(0), rate.GetRate(0));
	// 100 KiB delivered in 100 ms must not be presented as a 1000 KiB/s spike.
	rate.Add(100 * 1024, 100);
	ASSERT_EQUALS(uint64_t(100 * 1024), rate.GetRate(100));
}

TEST(DisplayRateCounterTest, SteadyTransferAndStop)
{
	CDisplayRateCounter rate;
	for (uint64_t now = 100; now <= 3000; now += 100) {
		rate.Add(30 * 1024, now);
		if (now >= 1000) {
			ASSERT_EQUALS(uint64_t(300 * 1024), rate.GetRate(now));
		}
	}
	ASSERT_EQUALS(uint64_t(150 * 1024), rate.GetRate(3500));
	// No new Add call: a reader must still expire a stopped transfer.
	ASSERT_EQUALS(uint64_t(0), rate.GetRate(4000));
	ASSERT_EQUALS(uint64_t(0), rate.GetRate(100000));
}

TEST(DisplayRateCounterTest, SpeedChangeReplacesOldWindow)
{
	CDisplayRateCounter rate;
	for (uint64_t now = 100; now <= 1000; now += 100) {
		rate.Add(10 * 1024, now);
	}
	for (uint64_t now = 1100; now <= 2000; now += 100) {
		rate.Add(40 * 1024, now);
	}
	ASSERT_EQUALS(uint64_t(400 * 1024), rate.GetRate(2000));
}

TEST(DisplayRateCounterTest, PeerTotalsDifferOnlyByControlTraffic)
{
	CDisplayRateCounter total, first, second;
	for (uint64_t now = 100; now <= 2000; now += 100) {
		first.Add(2000, now);
		second.Add(3000, now);
		total.Add(5000, now);
		total.Add(100, now); // control packets in the same millisecond
	}
	ASSERT_EQUALS(first.GetRate(2000) + second.GetRate(2000) + 1000, total.GetRate(2000));
	ASSERT_EQUALS(uint64_t(0), total.GetRate(3000));
}

TEST(DisplayRateCounterTest, NewUploadSessionStartsEmpty)
{
	CDisplayRateCounter rate;
	rate.Add(10000, 500);
	rate.Reset();
	ASSERT_EQUALS(uint64_t(0), rate.GetRate(500));
	rate.Add(500, 600);
	ASSERT_EQUALS(uint64_t(500), rate.GetRate(600));
}

TEST(DisplayRateCounterTest, WideTotalsAndReorderedProducers)
{
	CDisplayRateCounter rate;
	const uint64_t bytes = uint64_t(1) << 32;
	rate.Add(bytes, 500);
	rate.Add(bytes, 499); // a producer was delayed before acquiring the lock
	ASSERT_EQUALS(bytes * 2, rate.GetRate(500));
	ASSERT_EQUALS(bytes * 2, rate.GetRate(1499));
	ASSERT_EQUALS(uint64_t(0), rate.GetRate(1500));
}
