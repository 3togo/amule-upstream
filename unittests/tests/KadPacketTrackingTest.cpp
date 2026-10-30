// This file is part of the aMule Project.
// Copyright (c) 2003-2026 aMule Team ( https://amule-org.github.io )
// Licensed under the GNU General Public License, version 2 or later.
#include <muleunit/test.h>
#include <kademlia/net/PacketTracking.h>
#include <kademlia/kademlia/Kademlia.h>
#include <kademlia/routing/Contact.h>
#include <kademlia/routing/RoutingBin.h>
#include <RandomFunctions.h>
#include <Statistics.h>
#include <GetTickCount.h>
#include <protocol/kad2/Client2Client/UDP.h>
#include <protocol/kad/Client2Client/UDP.h>

using namespace muleunit;
using namespace Kademlia;
DECLARE_SIMPLE(KadPacketTracking)
namespace
{
uint64_t now = 0;
bool lan = false;
} // namespace
uint64 GetTickCount64()
{
	return now;
}
bool CKademlia::IsRunningInLANMode()
{
	return lan;
}
bool IsLanIP(uint32_t ip) noexcept
{
	return (ip & 0xff) == 10;
}
uint16_t CStatistics::s_kadNodesCur = 0;
uint16_t GetRandomUint16()
{
	return 0;
}

class Tracker : public CPacketTracking
{
public:
	using CPacketTracking::AddLegacyChallenge;
	using CPacketTracking::AddTrackedOutPacket;
	using CPacketTracking::HasActiveLegacyChallenge;
	using CPacketTracking::InTrackListIsAllowedPacket;
	using CPacketTracking::IsLegacyChallenge;
	using CPacketTracking::IsOnOutTrackList;
};
using Result = CPacketTracking::IncomingPacket;

TEST(KadPacketTracking, CapacityRejectsNewAddressesWithoutResettingLiveQuotas)
{
	now = 1000;
	lan = false;
	Tracker tracker;
	for (uint32_t ip = 1; ip <= Tracker::MAX_TRACKED_IPS; ++ip) {
		ASSERT_TRUE(tracker.InTrackListIsAllowedPacket(ip, KADEMLIA2_PING, false) == Result::Allowed);
	}
	ASSERT_TRUE(tracker.InTrackListIsAllowedPacket(10001, KADEMLIA2_PING, false) == Result::Drop);
	ASSERT_TRUE(tracker.InTrackListIsAllowedPacket(1, KADEMLIA2_PING, false) == Result::Allowed);
	ASSERT_TRUE(tracker.InTrackListIsAllowedPacket(1, KADEMLIA2_PING, false) == Result::Drop);
	// The response path remains usable even when request tracking is full.
	ASSERT_TRUE(tracker.InTrackListIsAllowedPacket(10001, KADEMLIA2_PONG, false) == Result::Allowed);
	now += 30000;
	ASSERT_TRUE(tracker.InTrackListIsAllowedPacket(10001, KADEMLIA2_PING, false) == Result::Allowed);
	ASSERT_TRUE(tracker.InTrackListIsAllowedPacket(1, KADEMLIA2_PING, false) == Result::Drop);
}

TEST(KadPacketTracking, SpoofedTrafficCannotPrimeTheVerifiedBanBudget)
{
	now = 1000;
	lan = false;
	Tracker tracker;
	for (unsigned i = 0; i < 100000; ++i) {
		ASSERT_FALSE(
			tracker.InTrackListIsAllowedPacket(1, KADEMLIA2_PING, false) == Result::MassiveFlood);
	}
	for (unsigned i = 0; i < 10; ++i) {
		ASSERT_TRUE(tracker.InTrackListIsAllowedPacket(1, KADEMLIA2_PING, true) == Result::Drop);
	}
	ASSERT_TRUE(tracker.InTrackListIsAllowedPacket(1, KADEMLIA2_PING, true) == Result::MassiveFlood);
	// Even extreme floods expire predictably once traffic stops.
	now += 6 * 60000;
	ASSERT_TRUE(tracker.InTrackListIsAllowedPacket(1, KADEMLIA2_PING, true) == Result::Allowed);
}

TEST(KadPacketTracking, LanModeAndOpcodeBudgetsStayIndependent)
{
	now = 1000;
	lan = true;
	Tracker tracker;
	for (unsigned i = 0; i < 100; ++i) {
		ASSERT_TRUE(tracker.InTrackListIsAllowedPacket(0x0a000001, KADEMLIA2_PING, true) ==
			    Result::Allowed);
	}
	lan = false;
	ASSERT_TRUE(
		tracker.InTrackListIsAllowedPacket(0x0a000001, KADEMLIA2_PING, true) == Result::MassiveFlood);
	ASSERT_TRUE(
		tracker.InTrackListIsAllowedPacket(0x0a000001, KADEMLIA2_HELLO_REQ, true) == Result::Allowed);
}

TEST(KadPacketTracking, OutgoingAndLegacyTrackingKeepRecentEntriesWithinTheirBounds)
{
	now = 1000;
	Tracker tracker;
	for (uint32_t ip = 1; ip <= Tracker::MAX_OUTGOING_REQUESTS + 1; ++ip) {
		tracker.AddTrackedOutPacket(ip, KADEMLIA2_PING);
	}
	ASSERT_FALSE(tracker.IsOnOutTrackList(1, KADEMLIA2_PING));
	ASSERT_TRUE(tracker.IsOnOutTrackList(2, KADEMLIA2_PING, true));
	ASSERT_TRUE(tracker.IsOnOutTrackList(2, KADEMLIA2_PING));
	ASSERT_FALSE(tracker.IsOnOutTrackList(2, KADEMLIA2_PING));
	for (uint32_t ip = 1; ip <= Tracker::MAX_LEGACY_CHALLENGES + 1; ++ip) {
		tracker.AddLegacyChallenge(CUInt128(ip), CUInt128(ip), ip, KADEMLIA2_PING);
	}
	ASSERT_FALSE(tracker.HasActiveLegacyChallenge(1));
	ASSERT_TRUE(tracker.HasActiveLegacyChallenge(2));
	CUInt128 id;
	ASSERT_FALSE(tracker.IsLegacyChallenge(CUInt128(3u), 2, KADEMLIA2_PING, id));
	ASSERT_TRUE(tracker.IsLegacyChallenge(CUInt128(2u), 2, KADEMLIA2_PING, id));
	ASSERT_TRUE(id == CUInt128(2u));
	now += 180000;
	ASSERT_FALSE(tracker.IsOnOutTrackList(3, KADEMLIA2_PING));
	ASSERT_FALSE(tracker.IsLegacyChallenge(CUInt128(3u), 3, KADEMLIA2_PING, id));
}

TEST(KadPacketTracking, ExpirationPreservesReferencesUntilRoutingCleanup)
{
	CContact contact(CUInt128(1u), 0x01020304, 4665, 4662, 8, CKadUDPKey(), true, CUInt128(0u));
	contact.UpdateType();
	CRoutingBin bin;
	bin.m_dontDeleteContacts = true;
	ASSERT_TRUE(bin.AddContact(&contact));
	ContactMap closest;
	bin.GetClosestTo(3, CUInt128(0u), 10, &closest);
	ASSERT_EQUALS(size_t(1), closest.size());
	ASSERT_TRUE(bin.GetRandomContact(3, 2) == &contact);
	ASSERT_TRUE(contact.GetType() < 4);
	contact.IncUse();
	contact.Expire();
	ASSERT_EQUALS(unsigned(4), unsigned(contact.GetType()));
	ASSERT_TRUE(contact.InUse());
	bin.GetClosestTo(3, CUInt128(0u), 10, &closest);
	ASSERT_TRUE(closest.empty());
	ASSERT_TRUE(bin.GetRandomContact(3, 2) == nullptr);
	ASSERT_TRUE(bin.GetContact(contact.GetClientID()) == &contact);
	ASSERT_TRUE(contact.GetExpireTime() > 0 && contact.GetExpireTime() <= time(nullptr));
	contact.DecUse();
	ASSERT_FALSE(contact.InUse());
	bin.RemoveContact(&contact);
	ASSERT_EQUALS(uint32_t(0), bin.GetSize());
}
