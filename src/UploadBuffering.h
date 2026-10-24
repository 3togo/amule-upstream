// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef UPLOADBUFFERING_H
#define UPLOADBUFFERING_H

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>

// Functional scheduling measurement, independent of optional diagnostic clocks.
// Slow samples raise the estimate immediately; faster samples decay gradually
// so a single page-cache hit cannot erase a recently observed storage stall.
class UploadReadLatency
{
public:
	void Observe(uint64_t milliseconds)
	{
		const auto sample = static_cast<uint32_t>(std::clamp<uint64_t>(milliseconds, 1, 10000));
		m_ms = sample >= m_ms ? sample : (m_ms * 7 + sample + 7) / 8;
	}
	uint32_t Milliseconds() const { return m_ms; }
	void Reset() { m_ms = 250; }

private:
	uint32_t m_ms = 250;
};

// Payload consumption times measured queue/read/handoff latency, plus a 50%
// latency margin (at least 100 ms) and one block. Preserve the hard payload cap.
inline uint32_t UploadBufferTarget(uint32_t bytesPerSecond, uint32_t blockSize, uint32_t latencyMs = 250)
{
	const uint64_t latency = std::min<uint64_t>(latencyMs, 10000);
	const uint64_t horizon = latency + std::max<uint64_t>(100, latency / 2);
	const uint64_t floor = std::min<uint64_t>(uint64_t{ blockSize } + 1, 4 * 1024 * 1024);
	return static_cast<uint32_t>(std::clamp<uint64_t>(
		uint64_t{ bytesPerSecond } * horizon / 1000 + floor, floor, 4 * 1024 * 1024));
}

// Conservative application-buffer accounting, not process RSS or kernel socket
// memory. Four payload copies plus 4 KiB per request cover read/copy/compression
// buffers and packet overhead; tiny requests cannot evade the memory budget.
// A reservation follows a block from admission through its last queued packet.
class UploadBufferBudget
{
public:
	static constexpr uint64_t kClientLimit = 16 * 1024 * 1024;
	static constexpr uint64_t kGlobalLimit = 128 * 1024 * 1024;
	struct Account
	{
		std::atomic<uint64_t> used{ 0 };
	};
	class Reservation
	{
	public:
		Reservation(std::shared_ptr<Account> global, std::shared_ptr<Account> client, uint64_t charge)
		: m_global(std::move(global))
		, m_client(std::move(client))
		, m_charge(charge)
		{
		}
		~Reservation()
		{
			m_client->used.fetch_sub(m_charge);
			m_global->used.fetch_sub(m_charge);
		}
		Reservation(const Reservation &) = delete;
		Reservation &operator=(const Reservation &) = delete;

	private:
		std::shared_ptr<Account> m_global;
		std::shared_ptr<Account> m_client;
		uint64_t m_charge;
	};
	using Lease = std::shared_ptr<Reservation>;
	enum class Denial
	{
		None,
		Client,
		Global
	};

	Lease Reserve(const std::shared_ptr<Account> &client, uint32_t payload, Denial *denial = nullptr)
	{
		if (denial != nullptr) {
			*denial = Denial::None;
		}
		const uint64_t charge = uint64_t{ payload } * 4 + 4096;
		if (payload == 0 || !Add(*client, charge, kClientLimit)) {
			if (denial != nullptr) {
				*denial = Denial::Client;
			}
			return {};
		}
		if (!Add(*m_global, charge, kGlobalLimit)) {
			client->used.fetch_sub(charge);
			if (denial != nullptr) {
				*denial = Denial::Global;
			}
			return {};
		}
		try {
			return std::make_shared<Reservation>(m_global, client, charge);
		} catch (...) {
			client->used.fetch_sub(charge);
			m_global->used.fetch_sub(charge);
			throw;
		}
	}
	uint64_t Used() const { return m_global->used.load(); }

private:
	static bool Add(Account &account, uint64_t charge, uint64_t limit)
	{
		uint64_t used = account.used.load();
		while (charge <= limit && used <= limit - charge) {
			if (account.used.compare_exchange_weak(used, used + charge)) {
				return true;
			}
		}
		return false;
	}
	std::shared_ptr<Account> m_global = std::make_shared<Account>();
};
#endif
