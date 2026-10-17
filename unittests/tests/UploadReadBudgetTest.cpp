// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later
#include <muleunit/test.h>
#include "UploadReadBudget.h"
#include <limits>

using namespace muleunit;
DECLARE_SIMPLE(UploadReadBudget)

TEST(UploadReadBudget, LargeQueuesDoNotTruncateTo32Bits)
{
	const int64_t bytes = (int64_t(1) << 32) + 1;
	ASSERT_EQUALS(static_cast<uint64_t>(bytes), UploadQueuedPayload(bytes, 0, 0));
	ASSERT_EQUALS(uint64_t(1) << 32, UploadQueuedPayload(bytes, 0, 1));
}

TEST(UploadReadBudget, SeparatelySampledCompletionsCannotOverflowOrUnderflow)
{
	const int64_t maximum = std::numeric_limits<int64_t>::max();
	ASSERT_EQUALS(0u, UploadQueuedPayload(maximum, maximum - 1, 2));
	ASSERT_EQUALS(0u, UploadQueuedPayload(1, 2, 0));
	ASSERT_EQUALS(0u, UploadQueuedPayload(1, 0, std::numeric_limits<uint64_t>::max()));
	ASSERT_EQUALS(30u, UploadQueuedPayload(100, 50, 20));
	ASSERT_EQUALS(0u, UploadQueuedPayload(0, 0, 0));
	ASSERT_EQUALS(0u, UploadQueuedPayload(-1, 0, 0));
}

TEST(UploadReadBudget, FastClientEventuallyYieldsEvenWhileItsSocketDrains)
{
	CUploadReadBudget budget(10 * 184320);
	unsigned reads = 0;
	while (budget.HasWorkAllowance()) {
		budget.Charge(184320);
		++reads;
	}
	ASSERT_EQUALS(10u, reads);
	// The next sweep has a fresh allowance; exhausting a sweep must not impose a timer delay.
	ASSERT_TRUE(CUploadReadBudget(10 * 184320).HasWorkAllowance());
}

TEST(UploadReadBudget, IndivisibleRequestExhaustsTheRemainingAllowance)
{
	CUploadReadBudget budget(2);
	budget.Charge(3);
	ASSERT_FALSE(budget.HasWorkAllowance());
	budget.Charge(std::numeric_limits<uint64_t>::max());
	ASSERT_FALSE(budget.HasWorkAllowance());
	ASSERT_FALSE(CUploadReadBudget(0).HasWorkAllowance());
}

TEST(UploadReadBudget, TinyRequestsCannotMonopolizeTheWorker)
{
	CUploadReadBudget budget(std::numeric_limits<uint64_t>::max(), 3);
	for (unsigned read = 0; read < 3; ++read) {
		ASSERT_TRUE(budget.HasWorkAllowance());
		budget.Charge(1);
	}
	ASSERT_FALSE(budget.HasWorkAllowance());
	budget.Charge(1);
	ASSERT_FALSE(budget.HasWorkAllowance());
	ASSERT_FALSE(CUploadReadBudget(100, 0).HasWorkAllowance());
}
