// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef PARTVERIFICATION_H
#define PARTVERIFICATION_H

#include <algorithm>
#include <cstdint>
#include <optional>

struct CPartVerificationRange
{
	uint64_t part;
	uint64_t offset;
	uint64_t length;
};

inline std::optional<CPartVerificationRange> PartVerificationRange(
	uint64_t part, uint64_t fileSize, uint64_t partSize)
{
	// Division before multiplication rejects invalid parts without overflow.
	if (partSize == 0 || fileSize == 0 || part > (fileSize - 1) / partSize) {
		return std::nullopt;
	}
	const uint64_t offset = part * partSize;
	return CPartVerificationRange{ part, offset, std::min(partSize, fileSize - offset) };
}

// Eligibility is separate from worker ownership, hashing and AICH recovery.
// Like Shareaza's verification-range interface, this selects a range without
// reading the file. Callers supply live dirty/completeness/write state.
// In particular, gap completeness alone does not establish disk-write completion.
// The tracked count bounds callbacks even if metadata and state arrays disagree.
template <typename NeedsHash, typename IsComplete, typename HasPendingWrite>
std::optional<CPartVerificationRange> FindNextPartToVerify(uint64_t fileSize,
	uint64_t partSize,
	uint64_t trackedPartCount,
	uint64_t firstPart,
	NeedsHash needsHash,
	IsComplete isComplete,
	HasPendingWrite hasPendingWrite)
{
	auto range = PartVerificationRange(firstPart, fileSize, partSize);
	while (range && range->part < trackedPartCount) {
		if (needsHash(range->part) && isComplete(range->part) && !hasPendingWrite(*range)) {
			return range;
		}
		// A nonempty range reaches EOF without needing an overflowing sentinel.
		if (range->length == fileSize - range->offset) {
			break;
		}
		range = PartVerificationRange(range->part + 1, fileSize, partSize);
	}
	return std::nullopt;
}
#endif
