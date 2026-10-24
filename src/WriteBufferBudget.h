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

// Request credits follow payload through reception, writes and retries. Each
// request also reserves two bounded assembly envelopes, so tiny packets never
// require an unreserved allocation per fragment after network admission.
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
		bool bytePressure = false, requestPressure = false;
		Totals::iterator byteIndex, requestIndex;
	};
	struct State
	{
		std::mutex mutex;
		std::map<std::string, std::shared_ptr<Account>> paths;
		std::map<std::string, Volume> volumes;
		Totals knownBytes, knownRequests;
		Counts used;
		bool globalBytes = false, globalRequests = false;
		bool unknownBytes = false, unknownRequests = false;

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
						try {
							found->second.requestIndex =
								knownRequests.insert(delta.requests);
						} catch (...) {
							knownBytes.erase(found->second.byteIndex);
							throw;
						}
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
			Totals::node_type byteNode, requestNode;
			if (!key.empty()) {
				byteNode = knownBytes.extract(total.byteIndex);
				requestNode = knownRequests.extract(total.requestIndex);
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
				requestNode.value() = total.requests;
				total.byteIndex = knownBytes.insert(std::move(byteNode));
				total.requestIndex = knownRequests.insert(std::move(requestNode));
			}
		}
		Counts Debt(const std::string &key) const
		{
			Counts total = GetVolume("");
			if (key.empty()) {
				total.bytes += knownBytes.empty() ? 0 : *knownBytes.rbegin();
				total.requests += knownRequests.empty() ? 0 : *knownRequests.rbegin();
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
	static constexpr uint64_t kGlobalRequests = 4096, kVolumeRequests = 1024;
	static constexpr unsigned kFragmentsPerRequest = 2;
	class Lease
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
			m_state->Change(account.volume, { m_bytes, 1, 0 }, false);
			m_state->used.bytes -= m_bytes;
			--m_state->used.requests;
			account.bytes -= m_bytes;
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
		// The allocation belongs to the request, not an individual snapshot.
		// The owner writes only previously unreceived bytes; immutable worker
		// bitmaps select disjoint bytes, so no second payload copy is needed.
		std::shared_ptr<uint8_t[]> Payload()
		{
			std::lock_guard<std::mutex> lock(m_payloadMutex);
			if (m_bytes > kGlobalLimit) {
				throw std::length_error("Write payload exceeds its hard bound.");
			}
			if (!m_payload) {
				m_payload.reset(new uint8_t[static_cast<size_t>(m_bytes)]);
			}
			return m_payload;
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
		std::shared_ptr<State> m_state;
		std::shared_ptr<Account> m_account;
		uint64_t m_bytes;
		unsigned m_fragments = 0;
		bool m_active = false;
		std::mutex m_payloadMutex;
		std::shared_ptr<uint8_t[]> m_payload;
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

	explicit WriteBufferBudget(uint64_t global = kGlobalLimit,
		uint64_t volume = kVolumeLimit,
		uint64_t globalRequests = kGlobalRequests,
		uint64_t volumeRequests = kVolumeRequests)
	: m_global(global)
	, m_volume(volume)
	, m_globalRequests(globalRequests)
	, m_volumeRequests(volumeRequests)
	{
	}
	static WriteBufferBudget &Get()
	{
		static WriteBufferBudget budget;
		return budget;
	}

	Credit Reserve(const std::string &path, uint64_t bytes, const std::string &volumeHint = {})
	{
		std::lock_guard<std::mutex> lock(m_state->mutex);
		if (bytes == 0 || bytes > m_global || m_state->used.bytes > m_global - bytes ||
			m_state->used.requests >= m_globalRequests) {
			return nullptr;
		}
		const auto found = m_state->paths.find(path);
		const std::string volume = found == m_state->paths.end() ? volumeHint : found->second->volume;
		const auto debt = m_state->Debt(volume);
		if (bytes > m_volume || debt.bytes > m_volume - bytes || debt.requests >= m_volumeRequests) {
			return nullptr;
		}
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
			m_state->Change(volume, { bytes, 1, 0 }, true);
		} catch (...) {
			if (account.requests == 0) {
				m_state->paths.erase(path);
			}
			throw;
		}
		account.bytes += bytes;
		++account.requests;
		m_state->used.bytes += bytes;
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
		std::lock_guard<std::mutex> lock(m_state->mutex);
		const bool globalBytes = UpdatePressure(m_state->used.bytes, m_global, m_state->globalBytes);
		const bool globalRequests =
			UpdatePressure(m_state->used.requests, m_globalRequests, m_state->globalRequests);
		const auto found = m_state->paths.find(path);
		const std::string key = found == m_state->paths.end() ? volumeHint : found->second->volume;
		const auto debt = m_state->Debt(key);
		bool bytes = false, requests = false;
		if (key.empty()) {
			bytes = UpdatePressure(debt.bytes, m_volume, m_state->unknownBytes);
			requests = UpdatePressure(debt.requests, m_volumeRequests, m_state->unknownRequests);
		} else {
			const auto volume = m_state->volumes.find(key);
			if (volume != m_state->volumes.end()) {
				bytes = UpdatePressure(debt.bytes, m_volume, volume->second.bytePressure);
				requests = UpdatePressure(
					debt.requests, m_volumeRequests, volume->second.requestPressure);
			} else {
				bytes = debt.bytes >= m_volume - m_volume / 4;
				requests = debt.requests >= m_volumeRequests - m_volumeRequests / 4;
			}
		}
		return globalBytes || globalRequests || bytes || requests;
	}

private:
	static bool UpdatePressure(uint64_t used, uint64_t limit, bool &pressured)
	{
		if (used >= limit - limit / 4) {
			pressured = true;
		} else if (used <= limit / 2) {
			pressured = false;
		}
		return pressured;
	}
	uint64_t m_global, m_volume, m_globalRequests, m_volumeRequests;
	std::shared_ptr<State> m_state = std::make_shared<State>();
};
#endif
