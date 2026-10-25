// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later
#include <muleunit/test.h>
#include "WriteBufferBudget.h"
#include <atomic>
#include <limits>
#include <thread>
#include <vector>

using namespace muleunit;
DECLARE_SIMPLE(WriteBufferBudget)

TEST(WriteBufferBudget, OutstandingRequestsDoNotConstrainTheNetworkPipeline)
{
	WriteBufferBudget budget(100, 50);
	std::vector<WriteBufferBudget::Credit> requests;
	for (unsigned i = 0; i < 2400; ++i) {
		requests.push_back(budget.ReserveRequest("a", 40));
		ASSERT_TRUE(requests.back() != nullptr);
	}
	ASSERT_EQUALS(uint64_t{ 0 }, budget.Used());
	ASSERT_FALSE(budget.IsPressured("a"));
	requests.clear();
	ASSERT_EQUALS(size_t{ 0 }, budget.Accounts());
	ASSERT_EQUALS(size_t{ 0 }, budget.Volumes());
}

TEST(WriteBufferBudget, MemoryIsChargedOnlyUntilTheLastBufferedOwnerDrains)
{
	WriteBufferBudget budget(100, 100);
	auto request = budget.ReserveRequest("a", 64);
	ASSERT_EQUALS(uint64_t{ 0 }, budget.Used());
	auto first = request->Payload();
	first[0] = 0x37;
	auto second = request->Payload();
	ASSERT_TRUE(first.get() == second.get());
	ASSERT_EQUALS(uint64_t{ 64 }, budget.Used());
	first.reset();
	ASSERT_EQUALS(0x37, static_cast<int>(second[0]));
	second.reset();
	ASSERT_EQUALS(uint64_t{ 0 }, budget.Used());
	ASSERT_EQUALS(uint64_t{ 1 }, budget.Requests());
	// The rest of this request is still in flight. A later fragment gets a
	// fresh charged buffer; already written bytes need not stay in memory.
	auto later = request->Payload();
	ASSERT_EQUALS(uint64_t{ 64 }, budget.Used());
	request.reset();
	ASSERT_EQUALS(uint64_t{ 1 }, budget.Requests());
	later.reset();
	ASSERT_EQUALS(uint64_t{ 0 }, budget.Used());
	ASSERT_EQUALS(uint64_t{ 0 }, budget.Requests());
}

TEST(WriteBufferBudget, AlreadyRequestedDataCanArriveAfterBytePressureStopsNewRequests)
{
	WriteBufferBudget budget(100, 50);
	std::vector<WriteBufferBudget::Credit> requests;
	for (int i = 0; i < 3; ++i) {
		requests.push_back(budget.ReserveRequest("a", 50, "disk"));
		ASSERT_TRUE(requests.back() != nullptr);
	}
	std::vector<std::shared_ptr<uint8_t[]>> payloads;
	payloads.push_back(requests[0]->Payload());
	ASSERT_TRUE(budget.IsPressured("a"));
	ASSERT_TRUE(budget.ReserveRequest("b", 40, "disk") != nullptr);
	payloads.push_back(requests[1]->Payload());
	payloads.push_back(requests[2]->Payload());
	ASSERT_EQUALS(uint64_t{ 150 }, budget.Used());
	ASSERT_TRUE(budget.ReserveRequest("independent", 40, "other") != nullptr);
	payloads.clear();
	ASSERT_EQUALS(uint64_t{ 0 }, budget.Used());
	ASSERT_FALSE(budget.IsPressured("a"));
	ASSERT_TRUE(budget.ReserveRequest("b", 40, "disk") != nullptr);
}

TEST(WriteBufferBudget, CancellationRetainsAllocatedPayloadUntilItsOwnersDrain)
{
	WriteBufferBudget budget(100, 50);
	std::vector<WriteBufferBudget::Credit> requests;
	for (int i = 0; i < 8; ++i) {
		requests.push_back(budget.ReserveRequest("a", 50, "disk"));
		ASSERT_TRUE(requests.back() != nullptr);
	}
	ASSERT_TRUE(budget.ReserveRequest("a", 1, "disk") != nullptr);
	std::vector<std::shared_ptr<uint8_t[]>> payloads;
	for (const auto &request : requests) {
		payloads.push_back(request->Payload());
	}
	ASSERT_EQUALS(uint64_t{ 400 }, budget.Used());
	requests.clear(); // cancellation cannot release buffered allocations
	ASSERT_EQUALS(uint64_t{ 8 }, budget.Requests());
	payloads.clear();
	ASSERT_EQUALS(uint64_t{ 0 }, budget.Used());
	ASSERT_EQUALS(uint64_t{ 0 }, budget.Requests());
}

TEST(WriteBufferBudget, PressureIsLocalUnlessUnknownDataOrTheGlobalTotalOverlaps)
{
	WriteBufferBudget budget(1000, 50);
	auto hot = budget.ReserveRequest("hot", 50, "a");
	auto healthy = budget.ReserveRequest("healthy", 8, "b");
	auto unresolved = budget.ReserveRequest("unknown", 10);
	auto hotData = hot->Payload();
	auto healthyData = healthy->Payload();
	auto unknownData = unresolved->Payload();
	ASSERT_TRUE(budget.IsPressured("hot"));
	ASSERT_FALSE(budget.IsPressured("healthy"));
	ASSERT_TRUE(budget.IsPressured("unknown"));
	ASSERT_TRUE(budget.ReserveRequest("other-b", 8, "b") != nullptr);
	ASSERT_TRUE(budget.Identify("unknown", "b"));
	ASSERT_FALSE(budget.Identify("unknown", "a"));
	ASSERT_FALSE(budget.IsPressured("healthy"));
	hotData.reset();
	ASSERT_FALSE(budget.IsPressured("hot"));
}

TEST(WriteBufferBudget, AtMostTwoSnapshotsRetainOwnershipAfterCancellation)
{
	WriteBufferBudget budget;
	auto request = budget.ReserveRequest("a", 4096, "disk");
	std::shared_ptr<uint8_t[]> payload;
	{
		WriteBufferBudget::Fragment first(request), second(request), third(request);
		ASSERT_TRUE(static_cast<bool>(first));
		ASSERT_TRUE(static_cast<bool>(second));
		ASSERT_FALSE(static_cast<bool>(third));
		payload = request->Payload();
		request.reset();
		ASSERT_EQUALS(uint64_t{ 2 }, budget.Fragments());
		ASSERT_EQUALS(uint64_t{ 4096 }, budget.Used());
	}
	ASSERT_EQUALS(uint64_t{ 0 }, budget.Fragments());
	ASSERT_EQUALS(uint64_t{ 1 }, budget.Requests());
	payload.reset();
	ASSERT_EQUALS(size_t{ 0 }, budget.Accounts());
	ASSERT_EQUALS(size_t{ 0 }, budget.Volumes());
}

TEST(WriteBufferBudget, InvalidRequestSizesNeverAllocateOrCreateAccounts)
{
	WriteBufferBudget budget;
	ASSERT_TRUE(budget.ReserveRequest("a", 0) == nullptr);
	ASSERT_TRUE(budget.ReserveRequest("a", WriteBufferBudget::kMaxRequestBytes + 1) == nullptr);
	ASSERT_TRUE(budget.ReserveRequest("a", std::numeric_limits<uint64_t>::max()) == nullptr);
	ASSERT_EQUALS(size_t{ 0 }, budget.Accounts());
	ASSERT_FALSE(budget.Identify("retired", "disk"));
}

TEST(WriteBufferBudget, ConcurrentBufferReleaseDoesNotLeakAccountsOrVolumeTotals)
{
	WriteBufferBudget budget;
	std::atomic<bool> failed{ false };
	std::vector<std::thread> workers;
	for (int i = 0; i < 4; ++i) {
		workers.emplace_back([&budget, &failed]() {
			for (int n = 0; n < 256; ++n) {
				auto request = budget.ReserveRequest("same", 32, "disk");
				if (!request) {
					failed = true;
					return;
				}
				auto payload = request->Payload();
				payload[0] = 0x42;
				request.reset();
			}
		});
	}
	for (auto &worker : workers) {
		worker.join();
	}
	ASSERT_FALSE(failed.load());
	ASSERT_EQUALS(uint64_t{ 0 }, budget.Used());
	ASSERT_EQUALS(uint64_t{ 0 }, budget.Requests());
	ASSERT_EQUALS(size_t{ 0 }, budget.Accounts());
	ASSERT_EQUALS(size_t{ 0 }, budget.Volumes());
}
