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

#include <muleunit/test.h>
#include "Connectivity.h"
#include "libs/ec/cpp/ECPacket.h"

using namespace muleunit;
using namespace Connectivity;
DECLARE_SIMPLE(Connectivity)

namespace
{
Snapshot Example()
{
	Snapshot s;
	s.readAt = 1793350800;
	s.tcpPort = 4662;
	s.udpPort = 4672;
	s.tcpListening = true;
	s.udpListening = true;
	s.udpEnabled = true;
	s.upnpAvailable = true;
	s.upnpEnabled = true;
	s.upnpRunning = true;
	s.router.detected = true;
	s.ed2k = Reachability::Reachable;
	s.kadTCP = Reachability::Reachable;
	s.kadUDP = Reachability::Blocked;
	s.bindInterface = "wg0";
	s.router.mappings = { { 4662, false, MappingState::Accepted, s.readAt - 10 },
		{ 4672, true, MappingState::Failed, s.readAt - 9 } };
	return s;
}
} // namespace

TEST(Connectivity, UnknownAndPendingNeverReportReachable)
{
	ASSERT_TRUE(Classify(true, true, false, false, false) == Reachability::Unverified);
	ASSERT_TRUE(Classify(true, true, true, true, false) == Reachability::Checking);
	ASSERT_TRUE(Classify(true, false, false, true, false) == Reachability::Disconnected);
	ASSERT_TRUE(Classify(false, true, false, true, false) == Reachability::Disabled);
	ASSERT_TRUE(Classify(true, true, false, true, false) == Reachability::Reachable);
	ASSERT_TRUE(Classify(true, true, false, true, true) == Reachability::Blocked);
}

TEST(Connectivity, MappingRefreshReplacesOutcomeAndKeepsOtherProtocol)
{
	MappingDiagnostics results;
	results.RouterDetected();
	results.Record(4662, false, true, 100);
	results.Record(4662, true, true, 101);
	const auto before = results.Get(); // Intentional snapshot before mutation.
	results.Record(4662, false, false, 102);
	const auto after = results.Get();
	ASSERT_TRUE(after.detected);
	ASSERT_EQUALS(size_t(2), after.mappings.size());
	ASSERT_TRUE(before.mappings[0].state == MappingState::Accepted);
	ASSERT_TRUE(after.mappings[0].state == MappingState::Failed);
	ASSERT_EQUALS(uint64_t(102), after.mappings[0].attemptedAt);
	ASSERT_TRUE(after.mappings[1].state == MappingState::Accepted);
	results.Forget(4662, false);
	ASSERT_EQUALS(size_t(1), results.Get().mappings.size());
	ASSERT_TRUE(results.Get().mappings[0].udp);
}

TEST(Connectivity, RemoteSnapshotMatchesLocalIncludingMixedNetworkStates)
{
	const auto source = Example();
	CECPacket packet(EC_OP_GET_CONNECTIVITY);
	AddTags(packet, source);
	Snapshot decoded;
	ASSERT_TRUE(Decode(packet, decoded));
	ASSERT_EQUALS(Format(source), Format(decoded));
	ASSERT_TRUE(decoded.ed2k == Reachability::Reachable);
	ASSERT_TRUE(decoded.kadUDP == Reachability::Blocked);
	ASSERT_EQUALS(wxString("wg0"), decoded.bindInterface);
	ASSERT_EQUALS(size_t(2), decoded.router.mappings.size());
}

TEST(Connectivity, MalformedReplyDoesNotReplaceLastSnapshot)
{
	Snapshot s = Example();
	CECPacket missing(EC_OP_GET_CONNECTIVITY);
	missing.AddTag(CECTag(EC_TAG_CONNECTIVITY_READ_AT, uint32_t(100)));
	ASSERT_FALSE(Decode(missing, s));
	ASSERT_EQUALS(uint16_t(4662), s.tcpPort);
	CECPacket wrongOpcode(EC_OP_FAILED);
	AddTags(wrongOpcode, Example());
	ASSERT_FALSE(Decode(wrongOpcode, s));
	Snapshot invalid = Example();
	invalid.kadUDP = static_cast<Reachability>(100);
	CECPacket invalidEnum(EC_OP_GET_CONNECTIVITY);
	AddTags(invalidEnum, invalid);
	ASSERT_FALSE(Decode(invalidEnum, s));
	invalid = Example();
	invalid.router.mappings.push_back(invalid.router.mappings.front());
	CECPacket duplicate(EC_OP_GET_CONNECTIVITY);
	AddTags(duplicate, invalid);
	ASSERT_FALSE(Decode(duplicate, s));
}

TEST(Connectivity, MappingAcceptanceCannotTurnBlockedOrUnknownEvidenceGreen)
{
	Snapshot s = Example();
	s.ed2k = Reachability::Disconnected;
	s.kadUDP = Reachability::Unverified;
	const wxString report = Format(s);
	ASSERT_FALSE(report.Contains("eD2k: High ID"));
	ASSERT_TRUE(report.Contains("Kad UDP: Unverified"));
	ASSERT_TRUE(report.Contains("Router accepted mapping"));
	ASSERT_TRUE(report.Contains("reachability still needs verification"));
}

TEST(Connectivity, DisabledMappingDoesNotShowAnOldSuccess)
{
	Snapshot s = Example();
	s.upnpEnabled = false;
	const wxString report = Format(s);
	ASSERT_TRUE(report.Contains("UPnP is disabled"));
	ASSERT_FALSE(report.Contains("Router accepted mapping"));
	s = Example();
	s.udpEnabled = false;
	s.kadUDP = Reachability::Disabled;
	s.router.mappings[1].state = MappingState::Accepted;
	ASSERT_TRUE(Format(s).Contains("UDP 4672: Disabled"));
}

TEST(Connectivity, RouterLossClearsAcceptedMappings)
{
	MappingDiagnostics diagnostics;
	diagnostics.RouterDetected();
	diagnostics.Record(4662, false, true, 100);
	diagnostics.RouterUnavailable();
	const auto lost = diagnostics.Get();
	ASSERT_FALSE(lost.detected);
	ASSERT_TRUE(lost.mappings.empty());
	diagnostics.RouterDetected();
	ASSERT_TRUE(diagnostics.Get().mappings.empty());
}

TEST(Connectivity, ServiceChangeCannotRetainAnotherServicesAcceptedResult)
{
	MappingDiagnostics diagnostics;
	diagnostics.RouterDetected();
	diagnostics.Record(4662, false, true, 100);
	diagnostics.RouterChanged(true); // Another service remains, with unknown mapping state.
	ASSERT_TRUE(diagnostics.Get().detected);
	ASSERT_TRUE(diagnostics.Get().mappings.empty());
}

TEST(Connectivity, UnavailableRouterOrBuildCannotDisplayStaleAcceptedMappings)
{
	Snapshot s = Example();
	s.router.detected = false;
	ASSERT_FALSE(Format(s).Contains("Router accepted mapping"));
	s = Example();
	s.upnpAvailable = false;
	ASSERT_FALSE(Format(s).Contains("Router accepted mapping"));
}

TEST(Connectivity, DuplicateFieldsAreRejectedWithoutReplacingTheSnapshot)
{
	Snapshot original = Example();
	CECPacket duplicate(EC_OP_GET_CONNECTIVITY);
	AddTags(duplicate, original);
	duplicate.AddTag(CECTag(EC_TAG_CONNECTIVITY_FLAGS, uint8_t(0)));
	ASSERT_FALSE(Decode(duplicate, original));
	ASSERT_TRUE(original.upnpEnabled);
	CECPacket duplicateString(EC_OP_GET_CONNECTIVITY);
	AddTags(duplicateString, original);
	duplicateString.AddTag(CECTag(EC_TAG_CONNECTIVITY_ADDRESS, wxString("other address")));
	ASSERT_FALSE(Decode(duplicateString, original));
	CECPacket duplicateChild(EC_OP_GET_CONNECTIVITY);
	AddTags(duplicateChild, original);
	CECEmptyTag record(EC_TAG_CONNECTIVITY_MAPPING);
	record.AddTag(CECTag(EC_TAG_CONNECTIVITY_TCP_PORT, uint16_t(10000)));
	record.AddTag(CECTag(EC_TAG_CONNECTIVITY_TCP_PORT, uint16_t(10001)));
	record.AddTag(CECTag(EC_TAG_CONNECTIVITY_FLAGS, uint8_t(1)));
	record.AddTag(CECTag(EC_TAG_CONNECTIVITY_MAPPING_STATE, uint8_t(1)));
	record.AddTag(CECTag(EC_TAG_CONNECTIVITY_READ_AT, uint64_t(100)));
	duplicateChild.AddTag(record);
	ASSERT_FALSE(Decode(duplicateChild, original));
}

TEST(Connectivity, RetryPolicyRejectsInactiveAndPendingNetworksButAllowsInconclusiveResults)
{
	Snapshot s = Example();
	ASSERT_TRUE(CanRecheckKad(s));
	s.kadTCP = Reachability::Checking;
	ASSERT_FALSE(CanRecheckKad(s));
	s.kadTCP = Reachability::Unverified;
	s.kadUDP = Reachability::Unverified;
	ASSERT_TRUE(CanRecheckKad(s)); // Timed out without evidence, so a retry is useful.
	s.udpEnabled = false;
	ASSERT_FALSE(CanRecheckKad(s));
	s = Example();
	s.lanMode = true;
	ASSERT_FALSE(CanRecheckKad(s));
	s = Example();
	s.kadTCP = Reachability::Disconnected;
	ASSERT_FALSE(CanRecheckKad(s));
}
