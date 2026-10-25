// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef UPLOADREADREUSE_H
#define UPLOADREADREUSE_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstddef>
#include <utility>

// Opt-in observations, without payload storage, paths, allocation or clocks.
// Reuse covers the most recently used distinct ranges in this bounded history.
class UploadReadReuse
{
public:
	static constexpr size_t kHistory = 256;
	struct Stats
	{
		uint64_t reads = 0, bytes = 0;
		uint64_t exactReads = 0, exactBytes = 0;
		uint64_t overlapReads = 0, overlapBytes = 0, evictions = 0;
	};
	void Observe(const uint8_t *hash, uint64_t start, uint64_t end)
	{
		if (hash == nullptr || end <= start) {
			return;
		}
		std::array<std::pair<uint64_t, uint64_t>, kHistory> overlaps{};
		size_t count = 0, exact = kHistory, empty = kHistory, oldest = 0;
		for (size_t i = 0; i < m_entries.size(); ++i) {
			const auto &entry = m_entries[i];
			if (entry.seen == 0) {
				if (empty == kHistory) {
					empty = i;
				}
				continue;
			}
			if (m_entries[oldest].seen == 0 || entry.seen < m_entries[oldest].seen) {
				oldest = i;
			}
			if (std::equal(entry.hash.begin(), entry.hash.end(), hash)) {
				if (entry.start == start && entry.end == end) {
					exact = i;
				}
				if (entry.start < end && entry.end > start) {
					overlaps[count++] = { std::max(start, entry.start),
						std::min(end, entry.end) };
				}
			}
		}
		const uint64_t bytes = end - start;
		uint64_t covered = 0, next = start;
		if (exact != kHistory) {
			covered = bytes;
		} else {
			std::sort(overlaps.begin(), overlaps.begin() + count);
			for (size_t i = 0; i < count; ++i) {
				const auto &span = overlaps[i];
				const auto first = std::max(next, span.first);
				if (span.second > first) {
					covered += span.second - first;
				}
				next = std::max(next, span.second);
			}
		}
		++m_stats.reads;
		m_stats.bytes += bytes;
		if (exact != kHistory) {
			++m_stats.exactReads;
			m_stats.exactBytes += bytes;
		}
		if (covered != 0) {
			++m_stats.overlapReads;
			m_stats.overlapBytes += covered;
		}
		const size_t index = exact != kHistory ? exact : (empty != kHistory ? empty : oldest);
		if (exact == kHistory && empty == kHistory) {
			++m_stats.evictions;
		}
		auto &entry = m_entries[index];
		std::copy_n(hash, entry.hash.size(), entry.hash.begin());
		entry.start = start;
		entry.end = end;
		entry.seen = ++m_sequence;
	}
	Stats Take()
	{
		const auto result = m_stats;
		m_stats = {};
		return result;
	}
	void Reset()
	{
		m_entries = {};
		m_stats = {};
		m_sequence = 0;
	}

private:
	struct Entry
	{
		std::array<uint8_t, 16> hash{};
		uint64_t start = 0, end = 0, seen = 0;
	};
	std::array<Entry, kHistory> m_entries{};
	Stats m_stats;
	uint64_t m_sequence = 0;
};
#endif
