// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef UPLOADREADBUDGET_H
#define UPLOADREADBUDGET_H

#include <cstdint>

// Session counters are signed, while socket completions are sampled separately. Subtract in
// stages so large queues cannot wrap at 4 GiB and adding completions cannot overflow int64_t.
inline uint64_t UploadQueuedPayload(int64_t added, int64_t sent, uint64_t completed)
{
	if (added <= 0) {
		return 0;
	}
	const uint64_t queued = sent <= 0 ? static_cast<uint64_t>(added)
					  : (added > sent ? static_cast<uint64_t>(added - sent) : 0);
	return completed < queued ? queued - completed : 0;
}

// One client's work per sweep. A request is indivisible, so the final read may exceed the
// remaining allowance by at most one request. Limit operations too: tiny requests must not
// monopolize the worker. Completed buffers are sent before the next read.
class CUploadReadBudget
{
public:
	explicit CUploadReadBudget(uint64_t bytes, uint32_t maxReads = 10)
	: m_remaining(bytes)
	, m_readsLeft(maxReads)
	{
	}
	bool HasWorkAllowance() const { return m_remaining != 0 && m_readsLeft != 0; }
	void Charge(uint64_t bytes)
	{
		m_remaining = bytes < m_remaining ? m_remaining - bytes : 0;
		if (m_readsLeft != 0) {
			--m_readsLeft;
		}
	}

private:
	uint64_t m_remaining;
	uint32_t m_readsLeft;
};

#endif // UPLOADREADBUDGET_H
