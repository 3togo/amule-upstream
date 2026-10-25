// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef UPLOADREADLATENCY_H
#define UPLOADREADLATENCY_H
#include <algorithm>
#include <cstdint>
// Functional scheduling measurement, independent of optional diagnostic clocks.
// Slow samples raise the estimate immediately; faster samples decay gradually
// so a single page-cache hit cannot erase a recently observed storage stall.
class UploadReadLatency
{
public:
	void Observe(uint64_t milliseconds)
	{
		const auto sample = static_cast<uint32_t>(std::clamp<uint64_t>(milliseconds, 1, 10000));
		m_ms = sample >= m_ms ? sample : (m_ms * 7 + sample + 7) / 8;
	}
	uint32_t Milliseconds() const { return m_ms; }
	void Reset() { m_ms = 250; }

private:
	uint32_t m_ms = 250;
};

// Payload consumption times measured queue/read/handoff latency, plus a 50%
// latency margin (at least 100 ms) and one block. Preserve the hard payload cap.
inline uint32_t UploadBufferTarget(uint32_t bytesPerSecond, uint32_t blockSize, uint32_t latencyMs = 250)
{
	const uint64_t latency = std::min<uint64_t>(latencyMs, 10000);
	const uint64_t horizon = latency + std::max<uint64_t>(100, latency / 2);
	const uint64_t floor = std::min<uint64_t>(uint64_t{ blockSize } + 1, 4 * 1024 * 1024);
	return static_cast<uint32_t>(std::clamp<uint64_t>(
		uint64_t{ bytesPerSecond } * horizon / 1000 + floor, floor, 4 * 1024 * 1024));
}

#endif
