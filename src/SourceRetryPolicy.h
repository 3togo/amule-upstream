// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SOURCERETRYPOLICY_H
#define SOURCERETRYPOLICY_H

#include <cstdint>
#include <limits>

// Keep retry policy independent of clients, sockets and the application clock.
// Shareaza's source lifecycle inspired this separation; these are aMule's
// existing eD2k intervals, not Shareaza's exponential backoff.
namespace SourceRetryPolicy
{
enum class Reason
{
	Reask,
	UdpProbe,
	NoNeededParts
};

inline uint64_t Delay(Reason reason, uint64_t reaskTime)
{
	switch (reason) {
	case Reason::UdpProbe:
		return reaskTime > 20000 ? reaskTime - 20000 : 0;
	case Reason::NoNeededParts:
		return reaskTime > std::numeric_limits<uint64_t>::max() / 2
			       ? std::numeric_limits<uint64_t>::max()
			       : reaskTime * 2;
	case Reason::Reask:
		return reaskTime;
	}
	return reaskTime;
}

inline bool IsDue(uint64_t now, uint64_t lastAsked, Reason reason, uint64_t reaskTime)
{
	// Zero retains the caller's explicit "ask on the next process tick" meaning.
	// A future timestamp must not underflow into an immediately eligible retry.
	return lastAsked == 0 || (now >= lastAsked && now - lastAsked > Delay(reason, reaskTime));
}

inline uint64_t DeadSourceDelay(bool global, bool lowID)
{
	return static_cast<uint64_t>((global ? 30 : 45) + (lowID ? 15 : 0)) * 60000;
}

inline uint64_t DeadSourceDeadline(uint64_t now, bool global, bool lowID)
{
	const uint64_t delay = DeadSourceDelay(global, lowID);
	return now > std::numeric_limits<uint64_t>::max() - delay ? std::numeric_limits<uint64_t>::max()
								  : now + delay;
}
} // namespace SourceRetryPolicy
#endif
