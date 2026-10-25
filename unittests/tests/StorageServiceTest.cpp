// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later
#include <muleunit/test.h>
#include "StorageService.h"
#include "StorageReadPool.h"
#include "VolumeWritePool.h"
#include <atomic>
#include <vector>
using namespace muleunit;
DECLARE_SIMPLE(StorageService)

TEST(StorageService, EarliestDeadlineAndSameDeviceExclusion)
{
	StorageService service;
	auto write = service.Queue("disk", StorageService::Kind::Write, 1000);
	auto read = service.Queue("disk", StorageService::Kind::Read, 100);
	ASSERT_FALSE(service.TryStart(write, 0));
	ASSERT_TRUE(service.TryStart(read, 0));
	ASSERT_FALSE(service.TryStart(write, 0));
	read.reset();
	ASSERT_TRUE(service.TryStart(write, 0));
	write.reset();
	ASSERT_EQUALS(size_t{ 0 }, service.Accounts());
}
TEST(StorageService, BothClassesReceiveServiceDuringContinuousUrgentWork)
{
	for (auto busy : { StorageService::Kind::Read, StorageService::Kind::Write }) {
		StorageService service;
		auto other = service.Queue("disk",
			busy == StorageService::Kind::Read ? StorageService::Kind::Write
							   : StorageService::Kind::Read,
			10000);
		for (unsigned i = 0; i < StorageService::kMaxBurst; ++i) {
			auto urgent = service.Queue("disk", busy, 0);
			ASSERT_TRUE(service.TryStart(urgent, 0));
		}
		auto urgent = service.Queue("disk", busy, 0);
		ASSERT_FALSE(service.TryStart(urgent, 0));
		ASSERT_TRUE(service.TryStart(other, 0));
		other.reset();
		ASSERT_TRUE(service.TryStart(urgent, 0));
	}
}
TEST(StorageService, PartitionsShareBackingDeviceAndRemappingDrainsActiveCalls)
{
	StorageService service(true);
	service.SetAliases({ { "a", "disk" }, { "b", "disk" }, { "c", "other" } });
	auto read = service.Queue("a", StorageService::Kind::Read, 0);
	auto write = service.Queue("b", StorageService::Kind::Write, 100);
	auto independent = service.Queue("c", StorageService::Kind::Read, 0);
	ASSERT_TRUE(service.TryStart(read, 0));
	ASSERT_FALSE(service.TryStart(write, 0));
	ASSERT_TRUE(service.TryStart(independent, 0));
	service.SetAliases({ { "a", "disk" }, { "b", "disk" }, { "c", "disk" } });
	read.reset();
	auto unaffected = service.Queue("unresolved", StorageService::Kind::Read, 0);
	ASSERT_FALSE(service.TryStart(unaffected, 0));
	unaffected.reset();
	ASSERT_FALSE(service.TryStart(write, 0));
	independent.reset();
	ASSERT_TRUE(service.TryStart(write, 0));
}
TEST(StorageService, UnknownAliasCannotOverlapOrBeStarved)
{
	StorageService service;
	auto known = service.Queue("a", StorageService::Kind::Read, 0);
	auto unknown = service.Queue("unknown", StorageService::Kind::Write, 100);
	ASSERT_TRUE(service.TryStart(known, 0));
	ASSERT_FALSE(service.TryStart(unknown, 200));
	auto next = service.Queue("b", StorageService::Kind::Read, 0);
	ASSERT_FALSE(service.TryStart(next, 200));
	known.reset();
	ASSERT_TRUE(service.TryStart(unknown, 200));
	ASSERT_FALSE(service.TryStart(next, 200));
	unknown.reset();
	ASSERT_TRUE(service.TryStart(next, 200));
}
TEST(StorageService, IndependentPoolsKeepWaitingReadsOffBusyDeviceAndDrainPermits)
{
	StorageService service;
	std::mutex mutex;
	std::condition_variable changed;
	bool entered = false, release = false;
	std::atomic<bool> same{ false }, other{ false };
	VolumeWritePool<int> writes([](int &) { return std::string("disk"); },
		[&](std::vector<int> &) {
			std::unique_lock<std::mutex> lock(mutex);
			entered = true;
			changed.notify_all();
			changed.wait_for(lock, std::chrono::seconds(5), [&] { return release; });
		},
		[](std::vector<int> &, const std::exception_ptr &) {},
		[](int &) { return false; },
		&service);
	StorageReadPool<int> reads(
		[&](int &id) {
			std::lock_guard<std::mutex> lock(mutex);
			(id == 1 ? same : other) = true;
			changed.notify_all();
		},
		[] {},
		&service);
	ASSERT_TRUE(writes.Submit("file", 0, "disk"));
	{
		std::unique_lock<std::mutex> lock(mutex);
		ASSERT_TRUE(changed.wait_for(lock, std::chrono::seconds(3), [&] { return entered; }));
	}
	auto a = std::make_unique<int>(1), b = std::make_unique<int>(2);
	ASSERT_TRUE(reads.Submit("disk", a));
	ASSERT_TRUE(reads.Submit("other", b));
	bool independent, blocked;
	{
		std::unique_lock<std::mutex> lock(mutex);
		independent = changed.wait_for(lock, std::chrono::seconds(3), [&] { return other.load(); });
		blocked = !same;
		release = true;
		changed.notify_all();
	}
	writes.Stop();
	reads.Stop();
	ASSERT_TRUE(independent && blocked && same);
	ASSERT_EQUALS(size_t{ 0 }, service.Accounts());
}

TEST(StorageService, ReadOwnershipSlotsAreSharedByPartitionsOfOneDisk)
{
	StorageService service(true);
	service.SetAliases({ { "a", "disk" }, { "b", "disk" }, { "c", "other" } });
	StorageReadPool<int> reads([](int &) {}, [] {}, &service);
	auto job = std::make_unique<int>(1);
	ASSERT_TRUE(reads.Submit("a", job));
	ASSERT_FALSE(reads.CanSubmit("b"));
	ASSERT_TRUE(reads.CanSubmit("c"));
	reads.Stop();
}
