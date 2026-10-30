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
#include "amule.h"
#include "Preferences.h"
#include "ListenSocket.h"
#include "ClientUDPSocket.h"
#include "ServerConnect.h"
#include "kademlia/kademlia/Kademlia.h"
#include "kademlia/kademlia/UDPFirewallTester.h"
#ifdef ENABLE_UPNP
#include "UPnPBase.h"
#endif

Connectivity::Snapshot Connectivity::Capture()
{
	Snapshot s;
	s.readAt = static_cast<uint64_t>(time(nullptr));
	s.tcpPort = thePrefs::GetPort();
	s.udpPort = thePrefs::GetUDPPort();
	s.udpEnabled = !thePrefs::IsUDPDisabled();
	s.tcpListening =
		s.tcpPort != 0 && theApp->listensocket && theApp->listensocket->BoundPort() == s.tcpPort;
	s.udpListening = s.udpEnabled && s.udpPort != 0 && theApp->clientudp &&
			 theApp->clientudp->BoundPort() == s.udpPort;
	s.bindAddress = thePrefs::GetAddress();
	s.bindInterface = thePrefs::GetNetworkInterface();
	s.upnpEnabled = thePrefs::GetUPnPEnabled();
#ifdef ENABLE_UPNP
	s.upnpAvailable = true;
	s.upnpRunning = theApp->m_upnp != nullptr;
	if (theApp->m_upnp)
		s.router = theApp->m_upnp->GetMappingDiagnostics();
#endif
	const bool ed2kConnected = theApp->serverconnect && theApp->serverconnect->IsConnected();
	s.ed2k = Classify(thePrefs::GetNetworkED2K(),
		ed2kConnected,
		false,
		true,
		ed2kConnected && theApp->serverconnect->GetClientID() < 0x01000000);
	using namespace Kademlia;
	s.lanMode = CKademlia::IsRunningInLANMode();
	const bool connected = CKademlia::IsConnected();
	const bool enabled = thePrefs::GetNetworkKademlia();
	const bool checkingTCP = connected && !s.lanMode && CKademlia::GetPrefs()->IsFirewallCheckRunning();
	const bool verifiedTCP = connected && !s.lanMode && CKademlia::GetPrefs()->IsFirewallCheckVerified();
	s.kadTCP = Classify(enabled, connected, checkingTCP, verifiedTCP, CKademlia::IsFirewalled());
	const bool canCheckUDP = enabled && connected && s.udpEnabled && !s.lanMode;
	const bool checkingUDP = canCheckUDP && CUDPFirewallTester::IsCheckPending();
	const bool verifiedUDP = canCheckUDP && CUDPFirewallTester::IsVerified();
	const bool blockedUDP = canCheckUDP && CUDPFirewallTester::IsFirewalledUDP(true);
	s.kadUDP = Classify(enabled && s.udpEnabled, connected, checkingUDP, verifiedUDP, blockedUDP);
	return s;
}

void Connectivity::RecheckKad()
{
	if (CanRecheckKad(Capture()))
		Kademlia::CKademlia::RecheckFirewalled();
}
