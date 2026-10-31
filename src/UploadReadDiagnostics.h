// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef UPLOADREADDIAGNOSTICS_H
#define UPLOADREADDIAGNOSTICS_H

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>

namespace UploadReadDiagnostics
{
// The coordinator publishes zero when disabled. A read spanning a toggle is
// excluded from the new interval; an untimed job never acquires timestamps.
template <typename Work, typename Clock>
void RunTimed(uint64_t epoch,
	const std::atomic<uint64_t> &activeEpoch,
	std::atomic<uint64_t> &started,
	uint64_t &finished,
	Work work,
	Clock clock)
{
	const bool timed = epoch != 0 && activeEpoch.load(std::memory_order_relaxed) == epoch;
	if (timed) {
		started.store(clock(), std::memory_order_relaxed);
	}
	const auto finish = [&] {
		if (timed && activeEpoch.load(std::memory_order_relaxed) == epoch) {
			finished = clock();
		}
	};
	try {
		work();
	} catch (...) {
		finish();
		throw;
	}
	finish();
}

// Millisecond buckets with power-of-two upper bounds. Median is an upper
// estimate (less than twice the observed value); maximum remains exact.
struct Histogram
{
	std::array<uint64_t, 66> buckets{};
	uint64_t count = 0;
	uint64_t maximum = 0;
	void Add(uint64_t value)
	{
		unsigned bucket = 0;
		uint64_t bound = 0;
		while (bound < value && bucket < 65) {
			++bucket;
			bound = bucket == 65 ? std::numeric_limits<uint64_t>::max()
					     : uint64_t{ 1 } << (bucket - 1);
		}
		++buckets[bucket];
		++count;
		if (value > maximum) {
			maximum = value;
		}
	}
	uint64_t Median() const
	{
		uint64_t cumulative = 0;
		for (unsigned i = 0; i < buckets.size(); ++i) {
			cumulative += buckets[i];
			if (cumulative >= (count + 1) / 2) {
				return i == 0 ? 0
					      : (i == 65 ? std::numeric_limits<uint64_t>::max()
							 : uint64_t{ 1 } << (i - 1));
			}
		}
		return 0;
	}
};
enum class SlotState
{
	Storage,
	Reading,
	Receiver,
	Peer
};
inline SlotState Classify(bool pending, bool requests, bool atTarget)
{
	if (pending) {
		return SlotState::Reading;
	}
	if (!requests) {
		return SlotState::Peer;
	}
	return atTarget ? SlotState::Receiver : SlotState::Storage;
}
// Retained per slot, so tracking another block needs no allocation. Identity
// includes the file: a peer can switch files without starting a new slot.
struct DispatchWait
{
	uint64_t since = 0;
	uint64_t session = 0;
	uint64_t begin = 0;
	uint64_t end = 0;
	std::array<uint8_t, 16> file{};
	bool active = false;
	void Observe(
		uint64_t newSession, uint64_t newBegin, uint64_t newEnd, const uint8_t *newFile, uint64_t now)
	{
		if (!active || session != newSession || begin != newBegin || end != newEnd ||
			!std::equal(file.begin(), file.end(), newFile)) {
			since = now;
			session = newSession;
			begin = newBegin;
			end = newEnd;
			std::copy(newFile, newFile + file.size(), file.begin());
		}
		active = true;
	}
	void Reset() { active = false; }
	uint64_t Elapsed(uint64_t now) const { return active && now >= since ? now - since : 0; }
};
struct Volume
{
	Histogram read;
	Histogram dispatch;
	uint64_t bytes = 0;
	uint64_t deliveryMaximum = 0;
	uint64_t turns = 0;
	uint64_t turnReads = 0;
	uint64_t busySkips = 0;
};
} // namespace UploadReadDiagnostics
#endif
