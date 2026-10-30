//
// This file is part of the aMule Project.
//
// Copyright (c) 2003-2026 aMule Team ( https://amule-org.github.io )
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
//

#ifndef KADEMLIA_LOOKUPPOLICY_H
#define KADEMLIA_LOOKUPPOLICY_H
#include "KadConstraints.h"
#include <cstdint>
#include <cstddef>
#include <map>
#include <utility>
namespace Kademlia
{
// Seeds have already passed routing admission. Count their actual contacts, then
// apply the same IP/subnet policy to referrals across the complete lookup.
class CLookupDiversity
{
public:
	explicit CLookupDiversity(const SKadConstraints &policy)
	: m_policy(policy)
	{
	}
	void AddSeed(uint32_t ip)
	{
		++m_ips[ip];
		++m_subnets[m_policy.Subnet(ip)];
	}
	bool Admit(uint32_t ip, bool lan)
	{
		const auto known = m_ips.find(ip);
		const auto subnet = m_subnets.find(m_policy.Subnet(ip));
		if ((known != m_ips.end() && known->second >= m_policy.contactsPerIP) ||
			(!lan && subnet != m_subnets.end() && subnet->second >= m_policy.contactsPerSubnet))
			return false;
		AddSeed(ip);
		return true;
	}

private:
	const SKadConstraints &m_policy;
	std::map<uint32_t, uint32_t> m_ips;
	std::map<uint32_t, uint32_t> m_subnets;
};

// One manager-owned budget covers every search, including retained late-reply
// records. Expiry is monotonic and releases capacity even for an idle search.
class CLookupBudget
{
public:
	explicit CLookupBudget(const SKadConstraints &policy)
	: m_policy(policy)
	{
	}
	bool CanSend(uint64_t now)
	{
		Prune(now);
		size_t active = 0;
		for (const auto &entry : m_pending)
			if (now < entry.second.deadline)
				++active;
		return m_pending.size() < m_policy.globalLookupPending &&
		       active < m_policy.globalLookupActive;
	}
	uint64_t Reserve(uint64_t now, uint32_t timeout)
	{
		if (!CanSend(now))
			return 0;
		do {
			++m_next;
		} while (m_next == 0 || m_pending.count(m_next) != 0);
		m_pending.emplace(m_next, Lease{ now + timeout, now + timeout + m_policy.sampleGraceMs });
		return m_next;
	}
	void Release(uint64_t token) noexcept { m_pending.erase(token); }
	void Prune(uint64_t now)
	{
		for (auto it = m_pending.begin(); it != m_pending.end();) {
			if (now >= it->second.expires)
				it = m_pending.erase(it);
			else
				++it;
		}
	}

private:
	struct Lease
	{
		uint64_t deadline;
		uint64_t expires;
	};
	const SKadConstraints &m_policy;
	uint64_t m_next = 0;
	std::map<uint64_t, Lease> m_pending;
};

class CLookupRequests
{
public:
	using Endpoint = std::pair<uint32_t, uint16_t>;
	CLookupRequests(CLookupBudget &budget, const SKadConstraints &policy)
	: m_budget(budget)
	, m_policy(policy)
	{
	}
	CLookupRequests(const CLookupRequests &) = delete;
	CLookupRequests &operator=(const CLookupRequests &) = delete;
	~CLookupRequests() { Clear(); }
	void Clear() noexcept
	{
		for (const auto &request : m_pending)
			m_budget.Release(request.second.token);
		m_pending.clear();
	}

	void Prune(uint64_t now)
	{
		for (auto it = m_pending.begin(); it != m_pending.end();) {
			if (now >= it->second.expires) {
				m_budget.Release(it->second.token);
				it = m_pending.erase(it);
			} else
				++it;
		}
	}
	bool CanSend(uint64_t now, size_t alpha)
	{
		Prune(now);
		if (m_pending.size() >= m_policy.lookupPending || !m_budget.CanSend(now))
			return false;
		size_t active = 0;
		for (const auto &request : m_pending)
			if (now < request.second.deadline)
				++active;
		return active < alpha;
	}
	bool Contains(Endpoint peer) const { return m_pending.count(peer) != 0; }
	bool Empty() const { return m_pending.empty(); }
	bool Sent(Endpoint peer, uint64_t now, uint32_t timeout, size_t alpha)
	{
		if (Contains(peer) || !CanSend(now, alpha))
			return false;
		const uint64_t token = m_budget.Reserve(now, timeout);
		if (token == 0)
			return false;
		try {
			m_pending.emplace(peer,
				Request{ now, now + timeout, now + timeout + m_policy.sampleGraceMs, token });
		} catch (...) {
			m_budget.Release(token);
			throw;
		}
		return true;
	}
	bool Complete(Endpoint peer, uint64_t now, uint32_t &elapsed)
	{
		const auto it = m_pending.find(peer);
		if (it == m_pending.end())
			return false;
		const bool useful = now < it->second.expires;
		elapsed = static_cast<uint32_t>(now - it->second.sent);
		m_budget.Release(it->second.token);
		m_pending.erase(it);
		return useful;
	}
	void Cancel(Endpoint peer) noexcept
	{
		const auto it = m_pending.find(peer);
		if (it != m_pending.end()) {
			m_budget.Release(it->second.token);
			m_pending.erase(it);
		}
	}

private:
	struct Request
	{
		uint64_t sent;
		uint64_t deadline;
		uint64_t expires;
		uint64_t token;
	};
	CLookupBudget &m_budget;
	const SKadConstraints &m_policy;
	std::map<Endpoint, Request> m_pending;
};
} // namespace Kademlia
#endif
