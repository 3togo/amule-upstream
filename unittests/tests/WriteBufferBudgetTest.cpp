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

TEST(WriteBufferBudget, GlobalAndAliasedVolumeLimits)
{
	WriteBufferBudget budget(30, 20);
	auto first = budget.Reserve("a", 15, "disk1");
	ASSERT_TRUE(first != nullptr);
	ASSERT_TRUE(budget.Reserve("alias", 6, "disk1") == nullptr);
	auto second = budget.Reserve("alias", 5, "disk1");
	auto third = budget.Reserve("b", 10, "disk2");
	ASSERT_TRUE(second != nullptr && third != nullptr);
	ASSERT_TRUE(budget.Reserve("b", 1, "disk2") == nullptr);
	ASSERT_EQUALS(uint64_t{ 30 }, budget.Used());
	first.reset();
	ASSERT_TRUE(budget.Reserve("b", 10, "disk2") != nullptr);
}

TEST(WriteBufferBudget, UnknownDebtCannotEscapeThroughDiscovery)
{
	WriteBufferBudget budget(100, 20);
	auto first = budget.Reserve("known", 12, "disk1");
	auto unknown = budget.Reserve("unresolved", 8);
	ASSERT_TRUE(first != nullptr && unknown != nullptr);
	ASSERT_TRUE(budget.Reserve("another", 1) == nullptr);
	ASSERT_TRUE(budget.Identify("unresolved", "disk1"));
	ASSERT_TRUE(budget.Reserve("known", 1, "disk1") == nullptr);
	ASSERT_TRUE(budget.Reserve("independent", 20, "disk2") != nullptr);
	ASSERT_EQUALS(uint64_t{ 20 }, budget.Used());
}

TEST(WriteBufferBudget, UnknownPathsShareOneLimitAndDebtCountsAgainstKnownVolumes)
{
	WriteBufferBudget budget(100, 20);
	auto first = budget.Reserve("one", 13);
	auto second = budget.Reserve("two", 7);
	ASSERT_TRUE(budget.Reserve("known", 1, "disk1") == nullptr);
	ASSERT_TRUE(budget.Reserve("three", 1) == nullptr);
	first.reset();
	ASSERT_TRUE(budget.Reserve("known", 13, "disk1") != nullptr);
}

TEST(WriteBufferBudget, RequestAndFragmentsRetainCreditUntilLastOwner)
{
	WriteBufferBudget budget(20, 20);
	auto request = budget.Reserve("path", 20);
	auto fragment = request;
	auto retry = fragment;
	request.reset();  // disconnect
	fragment.reset(); // one successful write
	ASSERT_TRUE(budget.Reserve("path", 1) == nullptr);
	retry.reset(); // final successful write or cancellation
	ASSERT_EQUALS(uint64_t{ 0 }, budget.Used());
	ASSERT_TRUE(budget.Reserve("path", 20) != nullptr);
}

TEST(WriteBufferBudget, RejectInvalidSizesAndLiveRemount)
{
	WriteBufferBudget budget(100, 20);
	ASSERT_TRUE(budget.Reserve("path", 0) == nullptr);
	ASSERT_TRUE(budget.Reserve("path", std::numeric_limits<uint64_t>::max()) == nullptr);
	auto lease = budget.Reserve("path", 20, "disk1");
	ASSERT_FALSE(budget.Identify("path", "disk2"));
	lease.reset();
	ASSERT_FALSE(budget.Identify("path", "disk2"));
	ASSERT_TRUE(budget.Reserve("path", 20, "disk2") != nullptr);
	ASSERT_EQUALS(uint64_t{ 0 }, budget.Used());
}

TEST(WriteBufferBudget, ConcurrentAdmissionsAndWorkerReleasesStayBounded)
{
	WriteBufferBudget budget(100, 20);
	std::atomic<bool> exceeded{ false };
	std::vector<std::thread> workers;
	for (unsigned i = 0; i < 8; ++i) {
		workers.emplace_back([&budget, &exceeded] {
			for (unsigned round = 0; round < 1000; ++round) {
				auto lease = budget.Reserve("path", 7);
				if (budget.Used() > 20) {
					exceeded.store(true);
				}
			}
		});
	}
	for (auto &worker : workers) {
		worker.join();
	}
	ASSERT_FALSE(exceeded.load());
	ASSERT_EQUALS(uint64_t{ 0 }, budget.Used());
}

TEST(WriteBufferBudget, PressureFlushesSmallFilesAndClearsAfterDrain)
{
	WriteBufferBudget budget(100, 20);
	auto first = budget.Reserve("a", 7, "disk1");
	auto second = budget.Reserve("b", 8, "disk1");
	ASSERT_TRUE(budget.IsPressured("a", "disk1"));
	first.reset();
	ASSERT_FALSE(budget.IsPressured("a", "disk1"));
	second.reset();
	ASSERT_FALSE(budget.IsPressured("a", "disk1"));
}

TEST(WriteBufferBudget, LocalPressureDoesNotFlushIndependentVolumes)
{
	WriteBufferBudget budget(100, 20);
	auto hot = budget.Reserve("hot", 11, "disk1");
	auto extra = budget.Reserve("hot", 4, "disk1");
	auto healthy = budget.Reserve("healthy", 2, "disk2");
	ASSERT_TRUE(budget.IsPressured("hot", "disk1"));
	ASSERT_TRUE(budget.IsPressured("alias", "disk1"));
	ASSERT_FALSE(budget.IsPressured("healthy", "disk2"));
	extra.reset();
	ASSERT_TRUE(budget.IsPressured("hot", "disk1")); // 55% retains the pressure latch
	hot.reset();
	ASSERT_FALSE(budget.IsPressured("hot", "disk1"));
	ASSERT_FALSE(budget.IsPressured("healthy", "disk2"));
}

TEST(WriteBufferBudget, UnknownPressureCountsOnlyPossibleAliasingDebt)
{
	WriteBufferBudget budget(100, 20);
	auto hot = budget.Reserve("hot", 3, "disk1");
	auto healthy = budget.Reserve("healthy", 2, "disk2");
	auto unknown = budget.Reserve("unknown", 12);
	ASSERT_TRUE(budget.IsPressured("hot", "disk1"));
	ASSERT_TRUE(budget.IsPressured("unknown"));
	ASSERT_FALSE(budget.IsPressured("healthy", "disk2"));
	unknown.reset();
	ASSERT_FALSE(budget.IsPressured("hot", "disk1"));
	ASSERT_FALSE(budget.IsPressured("unknown"));
}

TEST(WriteBufferBudget, GlobalPressureFlushesAllVolumesWithHysteresis)
{
	WriteBufferBudget budget(100, 40);
	auto a = budget.Reserve("a", 25, "disk1");
	auto b = budget.Reserve("b", 25, "disk2");
	auto c = budget.Reserve("c", 25, "disk3");
	ASSERT_TRUE(budget.IsPressured("idle", "disk4"));
	c.reset(); // 50% clears global pressure
	ASSERT_FALSE(budget.IsPressured("idle", "disk4"));
	ASSERT_FALSE(budget.IsPressured("a", "disk1"));
}

TEST(WriteBufferBudget, TinyRequestMetadataAndUnknownDebtHaveIndependentCaps)
{
	WriteBufferBudget budget(100, 40, 6, 2);
	std::vector<WriteBufferBudget::Credit> owned;
	owned.push_back(budget.Reserve("a", 1, "a"));
	owned.push_back(budget.Reserve("b", 1, "b"));
	auto unknown = budget.Reserve("unknown", 1);
	ASSERT_TRUE(unknown != nullptr);
	ASSERT_TRUE(budget.Reserve("alias", 1, "a") == nullptr);
	ASSERT_TRUE(budget.Reserve("more-unknown", 1) == nullptr);
	unknown.reset();
	owned.push_back(budget.Reserve("alias", 1, "a"));
	owned.push_back(budget.Reserve("b", 1, "b"));
	owned.push_back(budget.Reserve("c", 1, "c"));
	owned.push_back(budget.Reserve("c", 1, "c"));
	ASSERT_TRUE(std::all_of(
		owned.begin(), owned.end(), [](const auto &credit) { return credit != nullptr; }));
	ASSERT_TRUE(budget.Reserve("d", 1, "d") == nullptr);
	ASSERT_TRUE(budget.IsPressured("d", "d"));
	ASSERT_EQUALS(uint64_t{ 6 }, budget.Requests());
	ASSERT_EQUALS(uint64_t{ 6 }, budget.Used());
	owned.clear();
	ASSERT_EQUALS(size_t{ 0 }, budget.Accounts());
	ASSERT_EQUALS(size_t{ 0 }, budget.Volumes());
}
TEST(WriteBufferBudget, TwoFragmentEnvelopesAreReservedAndRetainRequestOwnership)
{
	WriteBufferBudget budget;
	auto request = budget.Reserve("path", 100);
	{
		WriteBufferBudget::Fragment first(request), second(request), third(request);
		ASSERT_TRUE(static_cast<bool>(first) && static_cast<bool>(second));
		ASSERT_FALSE(static_cast<bool>(third));
		ASSERT_TRUE(budget.Identify("path", "disk"));
		request.reset();
		ASSERT_EQUALS(uint64_t{ 100 }, budget.Used());
		ASSERT_EQUALS(uint64_t{ 2 }, budget.Fragments());
		ASSERT_TRUE(budget.Identify("absent", "disk") == false);
	}
	ASSERT_EQUALS(uint64_t{ 0 }, budget.Used());
	ASSERT_EQUALS(uint64_t{ 0 }, budget.Fragments());
	ASSERT_EQUALS(size_t{ 0 }, budget.Accounts());
}
TEST(WriteBufferBudget, HistoricalPathsAndVolumeIndexesDrainCompletely)
{
	WriteBufferBudget budget;
	for (unsigned i = 0; i < 10000; ++i) {
		auto request = budget.Reserve(std::to_string(i), 1, "disk-" + std::to_string(i));
		ASSERT_TRUE(request != nullptr);
		ASSERT_EQUALS(size_t{ 1 }, budget.Accounts());
	}
	ASSERT_EQUALS(size_t{ 0 }, budget.Accounts());
	ASSERT_EQUALS(size_t{ 0 }, budget.Volumes());
	ASSERT_EQUALS(uint64_t{ 0 }, budget.Requests());
}
TEST(WriteBufferBudget, IncrementalTotalsMatchARescanningModelThroughDiscoveryAndDrain)
{
	WriteBufferBudget budget(100, 40, 15, 7);
	struct Owned
	{
		WriteBufferBudget::Credit credit;
		std::string path;
		uint64_t bytes;
	};
	struct Account
	{
		std::string volume;
		uint64_t bytes = 0, count = 0;
	};
	std::vector<Owned> owned;
	std::map<std::string, Account> accounts;
	uint32_t random = 1;
	const auto next = [&]() {
		random = random * 1664525u + 1013904223u;
		return random;
	};
	for (unsigned operation = 0; operation < 3000; ++operation) {
		const auto choice = next() % 3;
		const auto path = "path-" + std::to_string(next() % 20);
		const auto hint = next() % 4 == 0 ? std::string() : "disk-" + std::to_string(next() % 3);
		if (choice == 0) {
			const uint64_t bytes = 1 + next() % 9;
			const auto found = accounts.find(path);
			const auto volume = found == accounts.end() ? hint : found->second.volume;
			std::map<std::string, std::pair<uint64_t, uint64_t>> totals;
			uint64_t used = 0, largestBytes = 0, largestCount = 0;
			for (const auto &entry : accounts) {
				auto &total = totals[entry.second.volume];
				total.first += entry.second.bytes;
				total.second += entry.second.count;
				used += entry.second.bytes;
			}
			for (const auto &entry : totals) {
				if (!entry.first.empty()) {
					largestBytes = std::max(largestBytes, entry.second.first);
					largestCount = std::max(largestCount, entry.second.second);
				}
			}
			const auto unknown = totals[""];
			const auto known =
				volume.empty() ? std::make_pair(largestBytes, largestCount) : totals[volume];
			const bool allowed = used + bytes <= 100 && owned.size() < 15 &&
					     unknown.first + known.first + bytes <= 40 &&
					     unknown.second + known.second < 7;
			auto credit = budget.Reserve(path, bytes, hint);
			ASSERT_EQUALS(allowed, credit != nullptr);
			if (credit) {
				auto &account = accounts[path];
				account.volume = volume;
				account.bytes += bytes;
				++account.count;
				owned.push_back({ std::move(credit), path, bytes });
			}
		} else if (choice == 1 && !owned.empty()) {
			const size_t index = next() % owned.size();
			auto &account = accounts.at(owned[index].path);
			account.bytes -= owned[index].bytes;
			if (--account.count == 0) {
				accounts.erase(owned[index].path);
			}
			owned.erase(owned.begin() + index);
		} else if (!hint.empty()) {
			const auto found = accounts.find(path);
			const bool allowed = found != accounts.end() &&
					     (found->second.volume.empty() || found->second.volume == hint);
			ASSERT_EQUALS(allowed, budget.Identify(path, hint));
			if (allowed) {
				found->second.volume = hint;
			}
		}
		uint64_t bytes = 0;
		for (const auto &entry : accounts) {
			bytes += entry.second.bytes;
		}
		ASSERT_EQUALS(bytes, budget.Used());
		ASSERT_EQUALS(static_cast<uint64_t>(owned.size()), budget.Requests());
		ASSERT_EQUALS(accounts.size(), budget.Accounts());
	}
	owned.clear();
	ASSERT_EQUALS(size_t{ 0 }, budget.Accounts());
	ASSERT_EQUALS(size_t{ 0 }, budget.Volumes());
}
