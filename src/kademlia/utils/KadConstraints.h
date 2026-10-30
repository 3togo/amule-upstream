//
// This file is part of the aMule Project.
//
// Copyright (c) 2003-2026 aMule Team ( https://amule-org.github.io )
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
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
//

#ifndef KADEMLIA_KADCONSTRAINTS_H
#define KADEMLIA_KADCONSTRAINTS_H

#include <algorithm>
#include <cstdint>
#include <limits>

namespace Kademlia
{
// One schema for local policy: field, config key, default, supported minimum/maximum.
// Bounds protect representation/arithmetic, rather than imposing another hidden policy.
#define KAD_CONSTRAINT_FIELDS(X) \
	X(trackedIPs, TrackedIPs, 10000, 1, INT32_MAX) \
	X(outgoingRequests, OutgoingRequests, 4096, 1, INT32_MAX) \
	X(legacyChallenges, LegacyChallenges, 1024, 1, INT32_MAX) \
	X(trackingLifetimeMs, TrackingLifetimeMs, 180000, 1, UINT32_MAX) \
	X(floodMultiplier, FloodMultiplier, 5, 1, (UINT32_MAX - 1) / 60000) \
	X(bootstrapRate, BootstrapRequestsPerMinute, 2, 1, 60000) \
	X(helloRate, HelloRequestsPerMinute, 3, 1, 60000) \
	X(routingRate, RoutingRequestsPerMinute, 10, 1, 60000) \
	X(searchRate, SearchRequestsPerMinute, 3, 1, 60000) \
	X(publishKeywordRate, PublishKeywordRequestsPerMinute, 3, 1, 60000) \
	X(publishSourceRate, PublishSourceRequestsPerMinute, 2, 1, 60000) \
	X(publishNotesRate, PublishNotesRequestsPerMinute, 2, 1, 60000) \
	X(firewallRate, FirewallRequestsPerMinute, 2, 1, 60000) \
	X(buddyRate, BuddyRequestsPerMinute, 2, 1, 60000) \
	X(callbackRate, CallbackRequestsPerMinute, 1, 1, 60000) \
	X(pingRate, PingRequestsPerMinute, 2, 1, 60000) \
	X(contactsPerIP, ContactsPerIP, 1, 1, INT32_MAX) \
	X(contactsPerSubnet, ContactsPerSubnet, 2, 1, INT32_MAX) \
	X(globalContactsPerSubnet, GlobalContactsPerSubnet, 10, 1, INT32_MAX) \
	X(subnetPrefix, SubnetPrefix, 24, 1, 32) \
	X(lookupConcurrency, LookupConcurrency, 5, 1, INT32_MAX) \
	X(nodeConcurrency, NodeConcurrency, 1, 1, INT32_MAX) \
	X(lookupPending, LookupPendingPerSearch, 64, 1, INT32_MAX) \
	X(globalLookupActive, LookupActiveGlobal, 0, 0, INT32_MAX) \
	X(globalLookupPending, LookupPendingGlobal, 0, 0, INT32_MAX) \
	X(requestTimeoutMs, RequestTimeoutMs, 3000, 1, UINT32_MAX - 1) \
	X(sampleGraceMs, LateReplyGraceMs, 10000, 1, UINT32_MAX) \
	X(wideningReasks, WideningReasks, 4, 0, INT32_MAX)

struct SKadConstraints
{
#define KAD_FIELD(field, key, value, minimum, maximum) uint32_t field = value;
	KAD_CONSTRAINT_FIELDS(KAD_FIELD)
#undef KAD_FIELD

	struct Setting
	{
		const char *key;
		uint32_t SKadConstraints::*member;
		uint32_t fallback;
		uint32_t minimum;
		uint32_t maximum;
	};
	static const auto &Settings()
	{
		static const Setting settings[] = {
#define KAD_SETTING(field, key, value, minimum, maximum) \
	{ #key, &SKadConstraints::field, value, minimum, maximum },
			KAD_CONSTRAINT_FIELDS(KAD_SETTING)
#undef KAD_SETTING
		};
		return settings;
	}

	bool automaticActive = true;
	bool automaticPending = true;
	// Zero global budgets select existing application connection/search budgets.
	void Resolve(uint32_t maxConnections, uint32_t sourceSearches)
	{
		automaticActive = globalLookupActive == 0;
		automaticPending = globalLookupPending == 0;
		if (globalLookupActive == 0) {
			const uint64_t desired =
				uint64_t(lookupConcurrency) * std::max(sourceSearches, uint32_t(1));
			globalLookupActive = static_cast<uint32_t>(
				std::min(desired, uint64_t(std::max(maxConnections, uint32_t(1)))));
		}
		globalLookupPending = globalLookupPending == 0
					      ? outgoingRequests
					      : std::min(globalLookupPending, outgoingRequests);
		globalLookupActive = std::min(globalLookupActive, globalLookupPending);
		lookupPending = std::min(lookupPending, globalLookupPending);
		lookupConcurrency = std::min(lookupConcurrency, std::min(lookupPending, globalLookupActive));
		nodeConcurrency = std::min(nodeConcurrency, lookupConcurrency);
		sampleGraceMs = std::min(sampleGraceMs, UINT32_MAX - requestTimeoutMs);
		contactsPerSubnet = std::min(contactsPerSubnet, globalContactsPerSubnet);
		contactsPerIP = std::min(contactsPerIP, contactsPerSubnet);
	}
	uint32_t Subnet(uint32_t ip) const { return ip & (UINT32_MAX << (32 - subnetPrefix)); }
};
#undef KAD_CONSTRAINT_FIELDS

// Application-owned, validated at preferences load and immutable during a session.
const SKadConstraints &GetKadConstraints();
} // namespace Kademlia
#endif
