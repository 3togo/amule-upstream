// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later
#include <muleunit/test.h>
#include "UploadReadLatency.h"
#include <limits>
#include <array>
#include <thread>
#include <vector>

using namespace muleunit;
DECLARE_SIMPLE(UploadReadLatency)

namespace
{
constexpr uint32_t block = 184320;
}

TEST(UploadReadLatency, TargetFollowsMeasuredLatencyWithFloorsAndCaps)
{
	ASSERT_EQUALS(block + 1, UploadBufferTarget(0, block, 5000));
	ASSERT_EQUALS(block + 1 + 375, UploadBufferTarget(1000, block, 250));
	ASSERT_EQUALS(block + 1 + 300000, UploadBufferTarget(100000, block, 2000));
	ASSERT_TRUE(
		UploadBufferTarget(1024 * 1024, block, 50) < UploadBufferTarget(1024 * 1024, block, 1000));
	ASSERT_EQUALS(4u * 1024 * 1024,
		UploadBufferTarget(
			std::numeric_limits<uint32_t>::max(), block, std::numeric_limits<uint32_t>::max()));
	ASSERT_EQUALS(4u * 1024 * 1024, UploadBufferTarget(1, std::numeric_limits<uint32_t>::max(), 1));
}
TEST(UploadReadLatency, SlowMeasurementRaisesReadAheadImmediatelyAndFastSamplesDecay)
{
	UploadReadLatency latency;
	latency.Observe(2000);
	ASSERT_EQUALS(2000u, latency.Milliseconds());
	const auto slow = UploadBufferTarget(100000, block, latency.Milliseconds());
	latency.Observe(1);
	ASSERT_TRUE(latency.Milliseconds() > 1500 && latency.Milliseconds() < 2000);
	ASSERT_TRUE(UploadBufferTarget(100000, block, latency.Milliseconds()) < slow);
	for (unsigned i = 0; i < 100; ++i) {
		latency.Observe(1);
	}
	ASSERT_TRUE(latency.Milliseconds() < 10);
	latency.Observe(std::numeric_limits<uint64_t>::max());
	ASSERT_EQUALS(10000u, latency.Milliseconds());
	latency.Reset();
	ASSERT_EQUALS(250u, latency.Milliseconds());
}
