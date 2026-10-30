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

#ifndef AMULE_CONNECTIVITY_H
#define AMULE_CONNECTIVITY_H

#include <wx/string.h>
#include <cstdint>
#include <ctime>
#include <mutex>
#include <vector>

class CECPacket;
class wxWindow;

namespace Connectivity
{
// Values are also carried over EC. Append new values rather than renumbering.
enum class Reachability : uint8_t
{
	Disabled,
	Disconnected,
	Checking,
	Reachable,
	Blocked,
	Unverified
};
enum class MappingState : uint8_t
{
	Unattempted,
	Accepted,
	Failed
};
struct Mapping
{
	uint16_t port = 0;
	bool udp = false;
	MappingState state = MappingState::Unattempted;
	uint64_t attemptedAt = 0;
};
struct RouterSnapshot
{
	bool detected = false;
	std::vector<Mapping> mappings;
};

// libupnp updates from callbacks; readers take a copy without touching its service maps.
class MappingDiagnostics
{
	mutable std::mutex m_mutex;
	RouterSnapshot m_snapshot;

public:
	void RouterDetected()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_snapshot.detected = true;
	}
	void RouterChanged(bool detected)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_snapshot = {};
		m_snapshot.detected = detected;
	}
	void RouterUnavailable() { RouterChanged(false); }
	void Record(uint16_t port, bool udp, bool accepted, uint64_t now)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		Mapping result{ port, udp, accepted ? MappingState::Accepted : MappingState::Failed, now };
		for (auto &mapping : m_snapshot.mappings) {
			if (mapping.port == port && mapping.udp == udp) {
				mapping = result;
				return;
			}
		}
		m_snapshot.mappings.push_back(result);
	}
	void Forget(uint16_t port, bool udp)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		for (auto it = m_snapshot.mappings.begin(); it != m_snapshot.mappings.end(); ++it) {
			if (it->port == port && it->udp == udp) {
				m_snapshot.mappings.erase(it);
				return;
			}
		}
	}
	RouterSnapshot Get() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_snapshot;
	}
};

inline Reachability Classify(bool enabled, bool connected, bool checking, bool verified, bool blocked)
{
	if (!enabled)
		return Reachability::Disabled;
	if (!connected)
		return Reachability::Disconnected;
	if (checking)
		return Reachability::Checking;
	if (!verified)
		return Reachability::Unverified;
	return blocked ? Reachability::Blocked : Reachability::Reachable;
}

struct Snapshot
{
	uint64_t readAt = 0;
	uint16_t tcpPort = 0;
	uint16_t udpPort = 0;
	bool tcpListening = false;
	bool udpListening = false;
	bool udpEnabled = false;
	bool upnpAvailable = false;
	bool upnpEnabled = false;
	bool upnpRunning = false;
	bool lanMode = false;
	wxString bindAddress;
	wxString bindInterface;
	Reachability ed2k = Reachability::Unverified;
	Reachability kadTCP = Reachability::Unverified;
	Reachability kadUDP = Reachability::Unverified;
	RouterSnapshot router;
};

inline bool CanRecheckKad(const Snapshot &s)
{
	return !s.lanMode && s.udpEnabled && s.kadTCP != Reachability::Disabled &&
	       s.kadTCP != Reachability::Disconnected && s.kadTCP != Reachability::Checking &&
	       s.kadUDP != Reachability::Checking;
}

// Core only. The snapshot is collected on the application's main thread.
Snapshot Capture();
void RecheckKad();
wxString Format(const Snapshot &snapshot);
void AddTags(CECPacket &packet, const Snapshot &snapshot);
bool Decode(const CECPacket &packet, Snapshot &snapshot);
#ifndef AMULE_DAEMON
void ShowAssistant(wxWindow *parent);
#endif
} // namespace Connectivity
#endif
