// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef BLOCKRESERVATION_H
#define BLOCKRESERVATION_H

#include <cstdint>
#include <optional>

struct CBlockRange
{
	uint64_t start;
	uint64_t end; // Inclusive, like the download gap and reservation lists.
};

// Find the first unreserved interval inside a candidate block. Reservations may arrive in
// any order and may overlap. ReadRange adapts the existing list without owning peer state.
// Never discard an entire block merely because another peer reserved part of it.
template <typename Reservations, typename ReadRange>
std::optional<CBlockRange> FindUnreservedBlock(
	CBlockRange candidate, const Reservations &reservations, ReadRange readRange)
{
	if (candidate.start > candidate.end) {
		return std::nullopt;
	}
	for (;;) {
		uint64_t end = candidate.end;
		bool advanced = false;
		for (const auto &reservation : reservations) {
			const CBlockRange reserved = readRange(reservation);
			if (reserved.start > reserved.end || reserved.end < candidate.start ||
				reserved.start > candidate.end) {
				continue;
			}
			if (reserved.start <= candidate.start) {
				if (reserved.end >= candidate.end) {
					return std::nullopt;
				}
				candidate.start = reserved.end + 1;
				advanced = true;
				break; // Restart: an earlier reservation may cover the new start.
			}
			if (reserved.start - 1 < end) {
				end = reserved.start - 1;
			}
		}
		if (!advanced) {
			return CBlockRange{ candidate.start, end };
		}
	}
}

#endif // BLOCKRESERVATION_H
