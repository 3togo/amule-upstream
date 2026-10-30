//
// This file is part of the aMule Project.
//
// Copyright (c) 2003-2026 aMule Team ( https://amule-org.github.io )
//
// Any parts of this program derived from the xMule, lMule or eMule project,
// or contributed by third-party developers are copyrighted by their
// respective authors.
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
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA
//

#include "Connectivity.h"
#include "libs/ec/cpp/ECPacket.h"
#include <common/Format.h>
#include <wx/datetime.h>
#include <wx/intl.h>

namespace
{

// Required scalar fields must be unambiguous; repeated mapping records are
// allowed, but duplicate fields within a record or snapshot are malformed.
const CECTag *UniqueTag(const CECTag &parent, ec_tagname_t name)
{
	const CECTag *found = nullptr;
	for (const auto &tag : parent) {
		if (tag.GetTagName() == name) {
			if (found)
				return nullptr;
			found = &tag;
		}
	}
	return found;
}

wxString StateText(Connectivity::Reachability state)
{
	using Connectivity::Reachability;
	switch (state) {
	case Reachability::Disabled:
		return _("Disabled");
	case Reachability::Disconnected:
		return _("Disconnected - connect this network to verify reachability");
	case Reachability::Checking:
		return _("Checking - waiting for peer responses");
	case Reachability::Reachable:
		return _("Reachable");
	case Reachability::Blocked:
		return _("Blocked / firewalled");
	case Reachability::Unverified:
		return _("Unverified");
	}
	return _("Unverified");
}
} // namespace

wxString Connectivity::Format(const Snapshot &s)
{
	// Format UTC explicitly; the core and a remote GUI may use different time zones.
	wxString out = CFormat(_("Core status read at: %s\n")) %
		       wxDateTime(static_cast<time_t>(s.readAt)).ToUTC().FormatISOCombined(' ');
	out += _("Times are UTC. This report uses the core's saved settings and current network state. Save "
		 "changes before checking again.\n\n");
	out += CFormat(_("eD2k: %s\n")) % (s.ed2k == Reachability::Reachable        ? _("High ID")
						  : s.ed2k == Reachability::Blocked ? _("Low ID")
										    : StateText(s.ed2k));
	out += CFormat(_("Kad TCP: %s\nKad UDP: %s\n\n")) % StateText(s.kadTCP) % StateText(s.kadUDP);
	out += CFormat(_("TCP %u listener: %s\nUDP %u listener: %s\n")) % s.tcpPort %
	       (s.tcpListening ? _("Listening") : _("Not listening")) % s.udpPort %
	       (!s.udpEnabled           ? _("Disabled")
		       : s.udpListening ? _("Listening")
					: _("Not listening"));
	out += CFormat(_("Configured bind address: %s\nConfigured network interface: %s\n\n")) %
	       (s.bindAddress.empty() ? _("Any") : s.bindAddress) %
	       (s.bindInterface.empty() ? _("Any") : s.bindInterface);
	out += _("Router port mapping\n");
	if (!s.upnpAvailable)
		out += _("This core was built without UPnP support.\n");
	else if (!s.upnpEnabled)
		out += _("UPnP is disabled. Enable it in Preferences > Connection to request automatic "
			 "router mapping.\n");
	else if (!s.upnpRunning)
		out += _("UPnP has not started. Restart the core after enabling UPnP; check the log if "
			 "initialization fails.\n");
	else if (!s.router.detected)
		out += _("No compatible router service has been discovered. Check that UPnP is enabled on "
			 "your router, or forward ports manually.\n");
	else
		out += _("Compatible router service discovered.\n");
	const Mapping ports[] = { { s.tcpPort, false },
		{ s.udpPort, true },
		{ static_cast<uint16_t>(s.tcpPort <= 65532 ? s.tcpPort + 3 : 0), true } };
	for (size_t i = 0; i < 3; ++i) {
		if (ports[i].port == 0)
			continue;
		Mapping mapping = ports[i];
		if (s.upnpAvailable && s.upnpEnabled && s.upnpRunning && s.router.detected) {
			for (const auto &result : s.router.mappings) {
				if (result.port == mapping.port && result.udp == mapping.udp)
					mapping = result;
			}
		}
		wxString state = _("Not attempted");
		if (i == 1 && !s.udpEnabled)
			state = _("Disabled");
		else if (mapping.state == MappingState::Accepted)
			state = _("Router accepted mapping - reachability still needs verification");
		else if (mapping.state == MappingState::Failed)
			state = _("Mapping request failed - see UPnP log for details");
		out += CFormat("%s %u: %s") % (mapping.udp ? "UDP" : "TCP") % mapping.port % state;
		if (mapping.attemptedAt)
			out += CFormat(_(" (attempted %s UTC)")) %
			       wxDateTime(static_cast<time_t>(mapping.attemptedAt))
				       .ToUTC()
				       .FormatISOCombined(' ');
		out += "\n";
	}
	out += _("\nNext steps\n");
	if (!s.tcpListening || (s.udpEnabled && !s.udpListening))
		out += _("- A local listener is unavailable. Check the log for a port conflict or invalid "
			 "bind address/interface before changing router rules.\n");
	if (s.ed2k == Reachability::Disconnected || s.kadTCP == Reachability::Disconnected)
		out += _("- Connect the enabled networks. Kad may need bootstrap nodes and time to find "
			 "peers before its checks can finish.\n");
	if (s.kadTCP == Reachability::Checking || s.kadUDP == Reachability::Checking)
		out += _("- Wait for peer responses. A pending check or a missing response alone does not "
			 "establish where traffic is blocked.\n");
	if (s.lanMode)
		out += _("- Kad is in LAN mode; Internet reachability is unverified.\n");
	if (!s.udpEnabled && s.kadTCP != Reachability::Disabled)
		out += _("- Enable the extended UDP port in Preferences > Connection for Kad.\n");
	if (s.ed2k == Reachability::Blocked || s.kadTCP == Reachability::Blocked ||
		s.kadUDP == Reachability::Blocked) {
		out += CFormat(_("- Allow inbound TCP %u and UDP %u in the core machine's firewall. If "
				 "mapping is unavailable, forward these ports to that machine's LAN address "
				 "and reserve its address with DHCP.\n")) %
		       s.tcpPort % s.udpPort;
		out += _("- For Docker, publish the TCP and UDP ports with their correct protocols. For a "
			 "VPN, use the provider's forwarded port; a mapping on your home router may not "
			 "apply to the VPN path.\n");
		out += _("- If forwarding is configured but reachability still fails, check for a second "
			 "router or ask your ISP whether you have CGNAT. These are possible causes, not a "
			 "diagnosis from this test.\n");
	}
	out += _("- eD2k High ID requires inbound TCP. Kad checks TCP and UDP separately. Server UDP (TCP+3) "
		 "is recommended for server queries and is not required for High ID.\n");
	out += _("- After changing forwarding or firewall rules, use Recheck Kad. For a fresh eD2k ID, "
		 "disconnect and reconnect eD2k from the main window.\n");
	out += _("- Address/interface binding changes require restarting the core. The values above "
		 "are configured values; a listener alone does not prove that the intended VPN route "
		 "is active.\n");
	return out;
}

void Connectivity::AddTags(CECPacket &packet, const Snapshot &s)
{
	packet.AddTag(CECTag(EC_TAG_CONNECTIVITY_READ_AT, s.readAt));
	packet.AddTag(CECTag(EC_TAG_CONNECTIVITY_TCP_PORT, s.tcpPort));
	packet.AddTag(CECTag(EC_TAG_CONNECTIVITY_UDP_PORT, s.udpPort));
	uint16_t flags = s.tcpListening | (s.udpListening << 1) | (s.udpEnabled << 2) |
			 (s.upnpAvailable << 3) | (s.upnpEnabled << 4) | (s.upnpRunning << 5) |
			 (s.router.detected << 6) | (s.lanMode << 7);
	packet.AddTag(CECTag(EC_TAG_CONNECTIVITY_FLAGS, flags));
	packet.AddTag(CECTag(EC_TAG_CONNECTIVITY_ED2K, static_cast<uint8_t>(s.ed2k)));
	packet.AddTag(CECTag(EC_TAG_CONNECTIVITY_KAD_TCP, static_cast<uint8_t>(s.kadTCP)));
	packet.AddTag(CECTag(EC_TAG_CONNECTIVITY_KAD_UDP, static_cast<uint8_t>(s.kadUDP)));
	packet.AddTag(CECTag(EC_TAG_CONNECTIVITY_ADDRESS, s.bindAddress));
	packet.AddTag(CECTag(EC_TAG_CONNECTIVITY_INTERFACE, s.bindInterface));
	for (const auto &m : s.router.mappings) {
		CECEmptyTag tag(EC_TAG_CONNECTIVITY_MAPPING);
		tag.AddTag(CECTag(EC_TAG_CONNECTIVITY_TCP_PORT, m.port));
		tag.AddTag(CECTag(EC_TAG_CONNECTIVITY_FLAGS, static_cast<uint8_t>(m.udp)));
		tag.AddTag(CECTag(EC_TAG_CONNECTIVITY_MAPPING_STATE, static_cast<uint8_t>(m.state)));
		tag.AddTag(CECTag(EC_TAG_CONNECTIVITY_READ_AT, m.attemptedAt));
		packet.AddTag(tag);
	}
}

bool Connectivity::Decode(const CECPacket &packet, Snapshot &snapshot)
{
	if (packet.GetOpCode() != EC_OP_GET_CONNECTIVITY)
		return false;
	Snapshot s;
	auto number = [](const CECTag &parent, ec_tagname_t name, uint64_t max, uint64_t &value) {
		const auto *tag = UniqueTag(parent, name);
		if (!tag || !tag->IsInt() || tag->GetInt() > max)
			return false;
		value = tag->GetInt();
		return true;
	};
	uint64_t n = 0;
	if (!number(packet, EC_TAG_CONNECTIVITY_READ_AT, UINT32_MAX, n))
		return false;
	s.readAt = n;
	if (!number(packet, EC_TAG_CONNECTIVITY_TCP_PORT, UINT16_MAX, n) || !n)
		return false;
	s.tcpPort = static_cast<uint16_t>(n);
	if (!number(packet, EC_TAG_CONNECTIVITY_UDP_PORT, UINT16_MAX, n))
		return false;
	s.udpPort = static_cast<uint16_t>(n);
	if (!number(packet, EC_TAG_CONNECTIVITY_FLAGS, 255, n))
		return false;
	s.tcpListening = n & 1;
	s.udpListening = n & 2;
	s.udpEnabled = n & 4;
	s.upnpAvailable = n & 8;
	s.upnpEnabled = n & 16;
	s.upnpRunning = n & 32;
	s.router.detected = n & 64;
	s.lanMode = n & 128;
	if (!number(packet, EC_TAG_CONNECTIVITY_ED2K, 5, n))
		return false;
	s.ed2k = static_cast<Reachability>(n);
	if (!number(packet, EC_TAG_CONNECTIVITY_KAD_TCP, 5, n))
		return false;
	s.kadTCP = static_cast<Reachability>(n);
	if (!number(packet, EC_TAG_CONNECTIVITY_KAD_UDP, 5, n))
		return false;
	s.kadUDP = static_cast<Reachability>(n);
	const auto *address = UniqueTag(packet, EC_TAG_CONNECTIVITY_ADDRESS);
	const auto *interface = UniqueTag(packet, EC_TAG_CONNECTIVITY_INTERFACE);
	if (!address || !address->IsString() || !interface || !interface->IsString())
		return false;
	s.bindAddress = address->GetStringData();
	s.bindInterface = interface->GetStringData();
	for (const auto &tag : packet) {
		if (tag.GetTagName() != EC_TAG_CONNECTIVITY_MAPPING)
			continue;
		if (s.router.mappings.size() >= 4)
			return false;
		Mapping m;
		if (!number(tag, EC_TAG_CONNECTIVITY_TCP_PORT, UINT16_MAX, n) || !n)
			return false;
		m.port = static_cast<uint16_t>(n);
		if (!number(tag, EC_TAG_CONNECTIVITY_FLAGS, 1, n))
			return false;
		m.udp = n;
		if (!number(tag, EC_TAG_CONNECTIVITY_MAPPING_STATE, 2, n))
			return false;
		m.state = static_cast<MappingState>(n);
		if (!number(tag, EC_TAG_CONNECTIVITY_READ_AT, UINT32_MAX, n))
			return false;
		m.attemptedAt = n;
		for (const auto &previous : s.router.mappings)
			if (previous.port == m.port && previous.udp == m.udp)
				return false;
		s.router.mappings.push_back(m);
	}
	snapshot = s;
	return true;
}
