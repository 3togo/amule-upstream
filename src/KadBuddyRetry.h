//
// This file is part of the aMule Project.
//
// Copyright (c) 2003-2026 aMule Team ( https://amule-org.github.io )
//
// Any parts of this program derived from the xMule, lMule or eMule project,
// or contributed by third-party developers are copyrighted by their
// respective authors.
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
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA
//

#ifndef KAD_BUDDY_RETRY_H
#define KAD_BUDDY_RETRY_H

#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <map>
#include <utility>

namespace Kademlia
{

// Outgoing buddy discovery only. All times are monotonic milliseconds.
class CBuddyRetry
{
public:
	static constexpr uint64_t CandidateCooldown = 60 * 60 * 1000;
	static constexpr uint64_t InitialSearchDelay = 60 * 1000;
	static constexpr uint64_t MaximumSearchDelay = 20 * 60 * 1000;
	static constexpr std::size_t MaximumFailedCandidates = 256;

	bool CanRequest(uint32_t ip, uint16_t port, uint64_t now) const
	{
		const auto entry = m_failed.find({ ip, port });
		return entry == m_failed.end() || now >= entry->second;
	}

	void ConnectionFailed(uint32_t ip, uint16_t port, uint64_t now)
	{
		for (auto it = m_failed.begin(); it != m_failed.end();) {
			if (now >= it->second) {
				it = m_failed.erase(it);
			} else {
				++it;
			}
		}
		const auto key = std::make_pair(ip, port);
		if (m_failed.count(key) == 0 && m_failed.size() >= MaximumFailedCandidates) {
			// Evict the earliest expiry, not the first IP in numeric order.
			m_failed.erase(std::min_element(m_failed.begin(),
				m_failed.end(),
				[](const auto &a, const auto &b) { return a.second < b.second; }));
		}
		m_failed[key] = now + CandidateCooldown;
	}

	bool CanStartSearch(uint64_t now) const { return !m_roundPending && now >= m_nextSearch; }
	void SearchStarted() { m_roundPending = true; }

	// A round includes the lookup and any TCP attempts it produced. One failed
	// candidate must not delay the other candidates in that same round.
	bool FinishRound(uint64_t now, bool lookupRunning, bool connecting)
	{
		if (!m_roundPending || lookupRunning || connecting) {
			return false;
		}
		m_roundPending = false;
		m_nextSearch = now + m_searchDelay;
		m_searchDelay = std::min(m_searchDelay * 2, MaximumSearchDelay);
		return true;
	}

	void Connected(uint32_t ip, uint16_t port)
	{
		m_failed.erase({ ip, port });
		m_roundPending = false;
		m_nextSearch = 0;
		m_searchDelay = InitialSearchDelay;
	}

private:
	std::map<std::pair<uint32_t, uint16_t>, uint64_t> m_failed;
	uint64_t m_nextSearch = 0;
	uint64_t m_searchDelay = InitialSearchDelay;
	bool m_roundPending = false;
};

} // namespace Kademlia

#endif // KAD_BUDDY_RETRY_H
