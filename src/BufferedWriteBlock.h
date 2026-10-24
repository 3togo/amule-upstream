// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef BUFFEREDWRITEBLOCK_H
#define BUFFEREDWRITEBLOCK_H

#include "FileArea.h"
#include "FileAutoClose.h"
#include "StorageService.h"
#include "WriteBufferBudget.h"
#include <algorithm>
#include <cstring>
#include <vector>

// One fixed request-span allocation plus a byte-validity bitmap replaces an
// allocation for every received fragment. Only the owner appends, and only while
// READY; accepted worker snapshots are immutable until result publication.
class BufferedWriteBlock
{
	WriteBufferBudget::Fragment m_fragment;

public:
	WriteBufferBudget::Credit writeCredit;
	CFileArea area;
	const uint64 start, end;
	const uint64 dirtyAt = StorageService::Now();
	BufferedWriteBlock(uint64 first, uint64 last, WriteBufferBudget::Credit credit)
	: m_fragment(credit)
	, writeCredit(std::move(credit))
	, start(first)
	, end(last)
	{
		if (!m_fragment || end < start || end - start >= CFile::kMaxWriteBytes ||
			Span() != writeCredit->PayloadBytes()) {
			throw CIOFailureException("Invalid bounded write assembly.");
		}
		m_valid.resize((Span() + 63) / 64);
		area.ShareWriteBuffer(writeCredit->Payload());
	}
	uint32 Bytes() const { return m_bytes; }
	size_t Span() const { return static_cast<size_t>(end - start + 1); }
	bool IsDense() const { return m_bytes == Span(); }
	// The caller supplies only missing gap intersections. Duplicate bytes do not
	// consume payload/metadata twice; bitmap updates need no allocation.
	uint32 Append(const uint8_t *data, uint64 offset, size_t count)
	{
		if (data == nullptr || offset < start || offset > end || count > end - offset + 1) {
			throw CIOFailureException("Fragment outside its reserved write assembly.");
		}
		const auto first = static_cast<size_t>(offset - start);
		std::memcpy(area.GetBuffer() + first, data, count);
		uint32 added = 0;
		for (size_t position = first; position < first + count;) {
			const size_t bit = position % 64;
			const size_t length = std::min<size_t>(64 - bit, first + count - position);
			const uint64 mask = (~uint64{ 0 } >> (64 - length)) << bit;
			auto &word = m_valid[position / 64];
			if ((word & mask) == 0) {
				added += static_cast<uint32>(length);
			} else {
				for (auto fresh = mask & ~word; fresh != 0; fresh &= fresh - 1) {
					++added;
				}
			}
			word |= mask;
			position += length;
		}
		m_bytes += added;
		return added;
	}
	// The caller holds the file mutex. Partial assemblies preserve holes with
	// one bounded read-modify-write; those holes stay missing in the gap list.
	// Original payload and bitmap survive every failed/short write for retry.
	void Flush(CFileAutoClose &file)
	{
		if (IsDense()) {
			area.FlushAt(file, start, Span());
			return;
		}
		auto merged = std::make_unique<uint8_t[]>(Span());
		const uint64 known = file.GetKnownSize();
		if (known > start) {
			file.ReadAt(merged.get(),
				start,
				static_cast<size_t>(std::min<uint64>(Span(), known - start)));
		}
		for (size_t position = 0; position < Span();) {
			if (!Valid(position)) {
				++position;
				continue;
			}
			const size_t first = position++;
			while (position < Span() && Valid(position)) {
				++position;
			}
			std::memcpy(merged.get() + first, area.GetBuffer() + first, position - first);
		}
		file.WriteAt(merged.get(), start, Span());
		area.Close();
	}

private:
	bool Valid(size_t position) const
	{
		return (m_valid[position / 64] & (uint64{ 1 } << (position % 64))) != 0;
	}
	std::vector<uint64> m_valid;
	uint32 m_bytes = 0;
};
#endif
