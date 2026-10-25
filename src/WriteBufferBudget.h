// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef WRITEBUFFERBUDGET_H
#define WRITEBUFFERBUDGET_H

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <stdexcept>

// Each requested span owns at most one live payload allocation and two write
// snapshots. Outstanding requests retain metadata only. Allocated payload alone
// applies backpressure; requests already owed by sources remain admissible.
class WriteBufferBudget
{
	using Totals = std::multiset<uint64_t>;
	struct Counts
	{
		uint64_t bytes = 0, requests = 0, fragments = 0;
	};
	struct Account : Counts
	{
		std::string volume, path;
	};
	struct Volume : Counts
	{
		bool bytePressure = false;
		Totals::iterator byteIndex;
	};
	struct State
	{
		std::mutex mutex;
		std::map<std::string, std::shared_ptr<Account>> paths;
		std::map<std::string, Volume> volumes;
		Totals knownBytes;
		Counts used;
		bool globalBytes = false;
		bool unknownBytes = false;

		Counts GetVolume(const std::string &key) const
		{
			const auto found = volumes.find(key);
			return found == volumes.end() ? Counts{} : static_cast<const Counts &>(found->second);
		}
		// Existing volume index nodes are extracted/reinserted without allocation:
		// release/destruction must not allocate merely to maintain the maxima.
		void Change(const std::string &key, Counts delta, bool add)
		{
			auto found = volumes.find(key);
			if (found == volumes.end()) {
				assert(add && delta.requests != 0);
				found = volumes.emplace(key, Volume{}).first;
				if (!key.empty()) {
					try {
						found->second.byteIndex = knownBytes.insert(delta.bytes);

					} catch (...) {
						volumes.erase(found);
						throw;
					}
				}
				static_cast<Counts &>(found->second) = delta;
				return;
			}
			auto &total = found->second;
			if (delta.bytes == 0 && delta.requests == 0) {
				if (add) {
					total.fragments += delta.fragments;
				} else {
					total.fragments -= delta.fragments;
				}
				return;
			}
			Totals::node_type byteNode;
			if (!key.empty()) {
				byteNode = knownBytes.extract(total.byteIndex);
			}
			if (add) {
				total.bytes += delta.bytes;
				total.requests += delta.requests;
				total.fragments += delta.fragments;
			} else {
				total.bytes -= delta.bytes;
				total.requests -= delta.requests;
				total.fragments -= delta.fragments;
			}
			if (total.requests == 0) {
				assert(total.bytes == 0 && total.fragments == 0);
				volumes.erase(found);
			} else if (!key.empty()) {
				byteNode.value() = total.bytes;
				total.byteIndex = knownBytes.insert(std::move(byteNode));
			}
		}
		Counts Debt(const std::string &key) const
		{
			Counts total = GetVolume("");
			if (key.empty()) {
				total.bytes += knownBytes.empty() ? 0 : *knownBytes.rbegin();
			} else {
				const auto known = GetVolume(key);
				total.bytes += known.bytes;
				total.requests += known.requests;
			}
			return total;
		}
	};

public:
	static constexpr uint64_t kGlobalLimit = 64 * 1024 * 1024;
	static constexpr uint64_t kVolumeLimit = 16 * 1024 * 1024;
	static constexpr uint64_t kMaxRequestBytes = 184320;
	static constexpr unsigned kFragmentsPerRequest = 2;
	class Lease : public std::enable_shared_from_this<Lease>
	{
		friend class WriteBufferBudget;

	public:
		~Lease()
		{
			if (!m_active) {
				return;
			}
			std::lock_guard<std::mutex> lock(m_state->mutex);
			assert(m_fragments == 0);
			auto &account = *m_account;
			m_state->Change(account.volume, { 0, 1, 0 }, false);
			--m_state->used.requests;
			--account.requests;
			if (account.requests == 0) {
				m_state->paths.erase(account.path);
			}
		}
		Lease(const Lease &) = delete;
		Lease &operator=(const Lease &) = delete;
		Lease(std::shared_ptr<State> state, std::shared_ptr<Account> account, uint64_t bytes)
		: m_state(std::move(state))
		, m_account(std::move(account))
		, m_bytes(bytes)
		{
		}
		uint64_t PayloadBytes() const { return m_bytes; }
		// Snapshots share one allocation while it is live. Keeping the request
		// alone retains no payload after its last write snapshot drains.
		std::shared_ptr<uint8_t[]> Payload()
		{
			std::lock_guard<std::mutex> lock(m_payloadMutex);
			if (auto existing = m_payload.lock()) {
				return existing;
			}
			auto allocation = std::make_shared<PayloadAllocation>(shared_from_this(), m_bytes);
			{
				std::lock_guard<std::mutex> stateLock(m_state->mutex);
				auto &account = *m_account;
				m_state->Change(account.volume, { m_bytes, 0, 0 }, true);
				account.bytes += m_bytes;
				m_state->used.bytes += m_bytes;
				allocation->charged = true;
			}
			std::shared_ptr<uint8_t[]> payload(allocation, allocation->data.get());
			m_payload = payload;
			return payload;
		}
		bool AddFragment()
		{
			std::lock_guard<std::mutex> lock(m_state->mutex);
			if (m_fragments >= kFragmentsPerRequest) {
				return false;
			}
			auto &account = *m_account;
			m_state->Change(account.volume, { 0, 0, 1 }, true);
			++account.fragments;
			++m_state->used.fragments;
			++m_fragments;
			return true;
		}
		void DropFragment()
		{
			std::lock_guard<std::mutex> lock(m_state->mutex);
			auto &account = *m_account;
			m_state->Change(account.volume, { 0, 0, 1 }, false);
			--account.fragments;
			--m_state->used.fragments;
			--m_fragments;
		}

	private:
		struct PayloadAllocation
		{
			PayloadAllocation(std::shared_ptr<Lease> owner, uint64_t bytes)
			: owner(std::move(owner))
			, data(std::make_unique<uint8_t[]>(static_cast<size_t>(bytes)))
			{
			}
			~PayloadAllocation()
			{
				if (!charged) {
					return;
				}
				data.reset();
				std::lock_guard<std::mutex> lock(owner->m_state->mutex);
				auto &account = *owner->m_account;
				owner->m_state->Change(account.volume, { owner->m_bytes, 0, 0 }, false);
				account.bytes -= owner->m_bytes;
				owner->m_state->used.bytes -= owner->m_bytes;
			}
			std::shared_ptr<Lease> owner;
			std::unique_ptr<uint8_t[]> data;
			bool charged = false;
		};
		std::shared_ptr<State> m_state;
		std::shared_ptr<Account> m_account;
		uint64_t m_bytes;
		unsigned m_fragments = 0;
		bool m_active = false;
		std::mutex m_payloadMutex;
		std::weak_ptr<uint8_t[]> m_payload;
	};
	using Credit = std::shared_ptr<Lease>;
	class Fragment
	{
	public:
		explicit Fragment(Credit credit)
		: m_credit(std::move(credit))
		{
			if (m_credit && !m_credit->AddFragment()) {
				m_credit.reset();
			}
		}
		~Fragment()
		{
			if (m_credit) {
				m_credit->DropFragment();
			}
		}
		Fragment(const Fragment &) = delete;
		Fragment &operator=(const Fragment &) = delete;
		explicit operator bool() const { return m_credit != nullptr; }

	private:
		Credit m_credit;
	};

	explicit WriteBufferBudget(uint64_t global = kGlobalLimit, uint64_t volume = kVolumeLimit)
	: m_global(global)
	, m_volume(volume)
	{
	}

	static WriteBufferBudget &Get()
	{
		static WriteBufferBudget budget;
		return budget;
	}

	Credit ReserveRequest(const std::string &path, uint64_t bytes, const std::string &volumeHint = {})
	{
		std::lock_guard<std::mutex> lock(m_state->mutex);
		if (bytes == 0 || bytes > kMaxRequestBytes) {
			return nullptr;
		}
		const auto found = m_state->paths.find(path);
		const std::string volume = found == m_state->paths.end() ? volumeHint : found->second->volume;
		auto owner = found == m_state->paths.end() ? std::make_shared<Account>() : found->second;
		if (owner->requests == 0) {
			owner->volume = volume;
			owner->path = path;
		}
		auto lease = std::make_shared<Lease>(m_state, owner, bytes);
		auto &account = *owner;
		if (found == m_state->paths.end()) {
			m_state->paths.emplace(path, owner);
		}
		try {
			m_state->Change(volume, { 0, 1, 0 }, true);
		} catch (...) {
			if (account.requests == 0) {
				m_state->paths.erase(path);
			}
			throw;
		}
		++account.requests;
		++m_state->used.requests;
		lease->m_active = true;
		return lease;
	}
	// Only reclassify live ownership. Future admissions obtain their identity
	// from the file session, rather than keeping historical zero-byte paths.
	bool Identify(const std::string &path, const std::string &volume)
	{
		if (volume.empty()) {
			return false;
		}
		std::lock_guard<std::mutex> lock(m_state->mutex);
		const auto found = m_state->paths.find(path);
		if (found == m_state->paths.end()) {
			return false;
		}
		auto &account = *found->second;
		if (!account.volume.empty()) {
			return account.volume == volume;
		}
		std::string next(volume);
		const Counts counts = account;
		m_state->Change(next, counts, true);
		m_state->Change(account.volume, counts, false);
		account.volume.swap(next);
		return true;
	}
	uint64_t Used() const
	{
		std::lock_guard<std::mutex> lock(m_state->mutex);
		return m_state->used.bytes;
	}
	uint64_t Requests() const
	{
		std::lock_guard<std::mutex> lock(m_state->mutex);
		return m_state->used.requests;
	}
	uint64_t Fragments() const
	{
		std::lock_guard<std::mutex> lock(m_state->mutex);
		return m_state->used.fragments;
	}
	size_t Accounts() const
	{
		std::lock_guard<std::mutex> lock(m_state->mutex);
		return m_state->paths.size();
	}
	size_t Volumes() const
	{
		std::lock_guard<std::mutex> lock(m_state->mutex);
		return m_state->volumes.size();
	}
	bool IsPressured(const std::string &path, const std::string &volumeHint = {}) const
	{
		return CheckPressure(path, false, volumeHint);
	}
	bool ShouldFlush(const std::string &path, const std::string &volumeHint = {}) const
	{
		return CheckPressure(path, true, volumeHint);
	}

private:
	bool CheckPressure(const std::string &path, bool early, const std::string &volumeHint) const
	{
		std::lock_guard<std::mutex> lock(m_state->mutex);
		const bool global =
			early ? m_state->used.bytes >= m_global / 8
			      : UpdatePressure(m_state->used.bytes, m_global, m_state->globalBytes);
		const auto found = m_state->paths.find(path);
		const std::string key = found == m_state->paths.end() ? volumeHint : found->second->volume;
		const auto debt = m_state->Debt(key);
		const auto group = m_state->volumes.find(key);
		if (!key.empty() && group == m_state->volumes.end()) {
			return global || debt.bytes >= (early ? m_volume / 8 : m_volume);
		}
		bool &pressure = key.empty() ? m_state->unknownBytes : group->second.bytePressure;
		return global ||
		       (early ? debt.bytes >= m_volume / 8 : UpdatePressure(debt.bytes, m_volume, pressure));
	}

private:
	static bool UpdatePressure(uint64_t used, uint64_t limit, bool &pressured)
	{
		if (used >= limit) {
			pressured = true;
		} else if (used <= limit / 2) {
			pressured = false;
		}
		return pressured;
	}
	uint64_t m_global, m_volume;
	std::shared_ptr<State> m_state = std::make_shared<State>();
};
#endif
