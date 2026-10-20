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

#ifndef DISPLAY_RATE_COUNTER_H
#define DISPLAY_RATE_COUNTER_H

#include <cstdint>
#include <deque>
#include <mutex>
#include <memory>

// Bytes in the last second, including an idle prefix at transfer startup. Separate from
// the longer averages used by statistics and transfer scheduling. Timestamps are monotonic
// milliseconds supplied by the caller, so expiry can also be tested without sleeping.
class CDisplayRateCounter
{
public:
	void Add(uint64_t bytes, uint64_t now)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		// Concurrent producers can acquire the lock out of timestamp order.
		if (m_samples && !m_samples->empty() && now < m_samples->back().time) {
			now = m_samples->back().time;
		}
		Expire(now);
		if (bytes) {
			if (!m_samples) {
				m_samples.reset(new std::deque<Sample>);
			}
			if (!m_samples->empty() && m_samples->back().time == now) {
				m_samples->back().bytes += bytes;
			} else {
				m_samples->push_back({now, bytes});
			}
			m_total += bytes;
		}
	}

	uint64_t GetRate(uint64_t now) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		Expire(now);
		// A one-second window makes its byte count equal to bytes per second.
		return m_total;
	}

	void Reset()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_samples.reset();
		m_total = 0;
	}

private:
	struct Sample { uint64_t time; uint64_t bytes; };
	void Expire(uint64_t now) const
	{
		while (m_samples && !m_samples->empty() && now >= m_samples->front().time &&
		       now - m_samples->front().time >= 1000) {
			m_total -= m_samples->front().bytes;
			m_samples->pop_front();
		}
		if (m_samples && m_samples->empty()) {
			m_samples.reset();
		}
	}
	mutable std::mutex m_mutex;
	// Most known peers never upload: allocate history only while bytes are in the window.
	mutable std::unique_ptr<std::deque<Sample>> m_samples;
	mutable uint64_t m_total = 0;
};

#endif // DISPLAY_RATE_COUNTER_H
