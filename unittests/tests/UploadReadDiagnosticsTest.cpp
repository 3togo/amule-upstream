// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later
#include <muleunit/test.h>
#include "UploadReadDiagnostics.h"
#include <limits>
#include <stdexcept>

using namespace muleunit;
using namespace UploadReadDiagnostics;
DECLARE_SIMPLE(UploadReadDiagnostics)

TEST(UploadReadDiagnostics, HistogramBoundsAndReset)
{
	Histogram histogram;
	ASSERT_EQUALS(uint64_t{ 0 }, histogram.Median());
	for (uint64_t value : { 0, 1, 3, 8, 9 }) {
		histogram.Add(value);
	}
	ASSERT_EQUALS(uint64_t{ 4 }, histogram.Median());
	ASSERT_EQUALS(uint64_t{ 9 }, histogram.maximum);
	ASSERT_EQUALS(uint64_t{ 5 }, histogram.count);
	histogram = {};
	ASSERT_EQUALS(uint64_t{ 0 }, histogram.count);
	histogram.Add(std::numeric_limits<uint64_t>::max());
	ASSERT_EQUALS(std::numeric_limits<uint64_t>::max(), histogram.Median());
}

TEST(UploadReadDiagnostics, EverySampleHasOneState)
{
	struct Case
	{
		bool pending;
		bool requests;
		bool atTarget;
		SlotState state;
	};
	const Case cases[] = { { false, false, false, SlotState::Peer },
		{ false, false, true, SlotState::Peer },
		{ false, true, false, SlotState::Storage },
		{ false, true, true, SlotState::Receiver },
		{ true, false, false, SlotState::Reading },
		{ true, false, true, SlotState::Reading },
		{ true, true, false, SlotState::Reading },
		{ true, true, true, SlotState::Reading } };
	for (const auto &sample : cases) {
		ASSERT_TRUE(Classify(sample.pending, sample.requests, sample.atTarget) == sample.state);
	}
}

TEST(UploadReadDiagnostics, TimingIsOptInAndDoesNotCrossIntervals)
{
	std::atomic<uint64_t> active{ 1 };
	std::atomic<uint64_t> started{ 0 };
	uint64_t finished = 0;
	unsigned clocks = 0;
	unsigned calls = 0;
	auto clock = [&] { return uint64_t{ ++clocks }; };
	auto work = [&] { ++calls; };
	RunTimed(0, active, started, finished, work, clock);
	ASSERT_EQUALS(0u, clocks);
	RunTimed(1, active, started, finished, work, clock);
	ASSERT_EQUALS(2u, clocks);
	ASSERT_EQUALS(uint64_t{ 1 }, started.load());
	ASSERT_EQUALS(uint64_t{ 2 }, finished);
	active = 0;
	RunTimed(1, active, started, finished, work, clock);
	ASSERT_EQUALS(2u, clocks);
	active = 2;
	RunTimed(1, active, started, finished, work, clock);
	ASSERT_EQUALS(2u, clocks);
	ASSERT_EQUALS(4u, calls);
	finished = 0;
	RunTimed(2, active, started, finished, [&] { active = 0; }, clock);
	ASSERT_EQUALS(3u, clocks);
	ASSERT_EQUALS(uint64_t{ 0 }, finished);
}

TEST(UploadReadDiagnostics, FailedReadsStillHaveTiming)
{
	std::atomic<uint64_t> active{ 1 };
	std::atomic<uint64_t> started{ 0 };
	uint64_t finished = 0;
	uint64_t now = 10;
	bool caught = false;
	try {
		RunTimed(
			1,
			active,
			started,
			finished,
			[] { throw std::runtime_error("read failed"); },
			[&] { return now++; });
	} catch (const std::runtime_error &) {
		caught = true;
	}
	ASSERT_TRUE(caught);
	ASSERT_EQUALS(uint64_t{ 10 }, started.load());
	ASSERT_EQUALS(uint64_t{ 11 }, finished);
}

TEST(UploadReadDiagnostics, HistogramCoversEveryPowerOfTwoBoundary)
{
	for (unsigned bit = 0; bit < 64; ++bit) {
		const uint64_t bound = uint64_t{ 1 } << bit;
		Histogram exact;
		exact.Add(bound);
		ASSERT_EQUALS(bound, exact.Median());
		if (bound > 1) {
			Histogram lower;
			lower.Add(bound - 1);
			ASSERT_EQUALS(bound == 2 ? uint64_t{ 1 } : bound, lower.Median());
		}
		Histogram upper;
		upper.Add(bound + 1);
		ASSERT_EQUALS(bit == 63 ? std::numeric_limits<uint64_t>::max() : bound << 1, upper.Median());
	}
}

TEST(UploadReadDiagnostics, DispatchWaitResetsForChangedRequestsAndInactiveSlots)
{
	DispatchWait wait;
	std::array<uint8_t, 16> first{};
	auto second = first;
	second[0] = 1;
	wait.Observe(1, 0, 10, first.data(), 0);
	ASSERT_EQUALS(uint64_t{ 20 }, wait.Elapsed(20));
	wait.Observe(1, 0, 10, first.data(), 20);
	ASSERT_EQUALS(uint64_t{ 30 }, wait.Elapsed(30));
	wait.Observe(1, 0, 10, second.data(), 30);
	ASSERT_EQUALS(uint64_t{ 10 }, wait.Elapsed(40));
	wait.Observe(2, 0, 10, second.data(), 40);
	ASSERT_EQUALS(uint64_t{ 10 }, wait.Elapsed(50));
	wait.Observe(2, 10, 20, second.data(), 50);
	ASSERT_EQUALS(uint64_t{ 10 }, wait.Elapsed(60));
	wait.Reset();
	ASSERT_EQUALS(uint64_t{ 0 }, wait.Elapsed(70));
	wait.Observe(2, 10, 20, second.data(), 80);
	ASSERT_EQUALS(uint64_t{ 5 }, wait.Elapsed(85));
}
