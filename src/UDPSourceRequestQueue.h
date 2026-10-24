// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef UDPSOURCEREQUESTQUEUE_H
#define UDPSOURCEREQUESTQUEUE_H

#include <cstdint>
#include <map>
#include <set>
#include <utility>

// Extra source passes are batched at most once a minute. A hash already queried
// by either kind of pass is not queued again for the regular reask interval.
// The owner supplies the monotonic clock and serializes access.
template <typename Hash> class UDPSourceRequestQueue
{
public:
	static constexpr uint64_t kPassGap = 60 * 1000;
	static constexpr uint64_t kFileCooldown = 1300 * 1000;

	void Queue(const Hash &hash, uint64_t now)
	{
		for (auto it = m_requested.begin(); it != m_requested.end();) {
			if (now - it->second >= kFileCooldown) {
				it = m_requested.erase(it);
			} else {
				++it;
			}
		}
		if (!m_requested.count(hash)) {
			m_pending.insert(hash);
		}
	}
	bool Ready(uint64_t now) const
	{
		return !m_pending.empty() && (!m_started || now - m_lastPass >= kPassGap);
	}
	bool Begin(uint64_t now, bool full, std::set<Hash> &targets)
	{
		if (!full && !Ready(now)) {
			return false;
		}
		targets.clear();
		if (!full) {
			targets.swap(m_pending);
		}
		m_pending.clear();
		m_lastPass = now;
		m_started = true;
		return true;
	}
	void Requested(const Hash &hash, uint64_t now)
	{
		m_requested[hash] = now;
		// A file queued after this pass began may have missed earlier servers.
		// Preserve that follow-up even if later servers see it in this pass.
	}
	void Interrupted(const std::set<Hash> &targets)
	{
		// Retry the interrupted pass even if some servers already saw its hashes.
		m_pending.insert(targets.begin(), targets.end());
	}
	void Remove(const Hash &hash) { m_pending.erase(hash); }
	void Clear() { m_pending.clear(); }

private:
	std::set<Hash> m_pending;
	std::map<Hash, uint64_t> m_requested;
	uint64_t m_lastPass = 0;
	bool m_started = false;
};
#endif
