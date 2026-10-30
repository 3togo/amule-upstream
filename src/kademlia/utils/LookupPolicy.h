// This file is part of the aMule Project.
// Copyright (c) 2003-2026 aMule Team ( https://amule-org.github.io )
// Licensed under the GNU General Public License, version 2 or later.

#ifndef KADEMLIA_LOOKUPPOLICY_H
#define KADEMLIA_LOOKUPPOLICY_H

#include <cstdint>
#include <cstddef>
#include <map>
#include <set>
#include <utility>

namespace Kademlia
{
// Routing-table seeds already passed routing admission. Count them as well, but do not
// discard them: subnet diversity limits apply to new referrals across the whole lookup.
class CLookupDiversity
{
public:
	void AddSeed(uint32_t ip)
	{
		if (m_ips.insert(ip).second) {
			++m_subnets[ip & 0xffffff00];
		}
	}

	bool Admit(uint32_t ip, bool lan)
	{
		const auto subnet = m_subnets.find(ip & 0xffffff00);
		if (m_ips.count(ip) != 0 || (!lan && subnet != m_subnets.end() && subnet->second >= 2)) {
			return false;
		}
		AddSeed(ip);
		return true;
	}

private:
	std::set<uint32_t> m_ips;
	std::map<uint32_t, unsigned> m_subnets;
};

// Soft deadlines free a scheduling slot while preserving late-response samples. Deadlines
// are fixed at send time; later samples and replies from other peers cannot move them.
class CLookupRequests
{
public:
	using Endpoint = std::pair<uint32_t, uint16_t>;
	static constexpr uint64_t SAMPLE_GRACE_MS = 10000;
	static constexpr size_t MAX_PENDING = 64;

	void Prune(uint64_t now)
	{
		for (auto it = m_pending.begin(); it != m_pending.end();) {
			if (now >= it->second.deadline + SAMPLE_GRACE_MS) {
				it = m_pending.erase(it);
			} else {
				++it;
			}
		}
	}

	bool CanSend(uint64_t now, size_t alpha) const
	{
		if (m_pending.size() >= MAX_PENDING) {
			return false;
		}
		size_t active = 0;
		for (const auto &request : m_pending) {
			if (now < request.second.deadline) {
				++active;
			}
		}
		return active < alpha;
	}

	bool Contains(Endpoint peer) const { return m_pending.count(peer) != 0; }
	bool Empty() const { return m_pending.empty(); }

	void Sent(Endpoint peer, uint64_t now, uint32_t timeout)
	{
		m_pending.emplace(peer, Request{ now, now + timeout });
	}

	bool Complete(Endpoint peer, uint64_t now, uint32_t &elapsed)
	{
		auto it = m_pending.find(peer);
		if (it == m_pending.end()) {
			return false;
		}
		const bool useful = now < it->second.deadline + SAMPLE_GRACE_MS;
		elapsed = static_cast<uint32_t>(now - it->second.sent);
		m_pending.erase(it);
		return useful;
	}

private:
	struct Request
	{
		uint64_t sent;
		uint64_t deadline;
	};
	std::map<Endpoint, Request> m_pending;
};
} // namespace Kademlia

#endif
