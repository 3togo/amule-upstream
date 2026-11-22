//
// This file is part of the aMule Project.
//
// Parts of this file are based on work from pan One (http://home-3.tiscali.nl/~meost/pms/)
//
// Copyright (c) 2003-2026 aMule Team ( https://amule-org.github.io )
// Copyright (c) 2002-2011 Merkur ( devs@emule-project.net / http://www.emule-project.net )
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

#ifndef SOURCEEXCHANGEPACKET_H
#define SOURCEEXCHANGEPACKET_H

#include "Logger.h"
#include "MemFile.h"
#include "Packet.h"
#include <common/Format.h>
#include <protocol/Protocols.h>
#include <protocol/ed2k/Constants.h>
#include <protocol/ed2k/Client2Client/TCP.h>
#include <algorithm>

// Shared wire format for complete and partial files. Callers retain source eligibility and
// legacy ID selection, which differ between upload and download sources.
class CSourceExchangePacket
{
public:
	CSourceExchangePacket(const CMD4Hash &fileHash,
		uint8 sx1Version,
		bool supportsSX2,
		uint8 requestedVersion,
		uint16 requestedOptions)
	: m_data(1024)
	, m_sx2(supportsSX2 && requestedVersion > 0)
	, m_version(m_sx2 ? std::min(requestedVersion, uint8(SOURCEEXCHANGE2_VERSION)) : sx1Version)
	{
		if (m_sx2) {
			m_data.WriteUInt8(m_version);
			if (requestedOptions != 0) {
				AddDebugLogLineN(logKnownFiles,
					CFormat("Client requested unknown options for SourceExchange2: %u") %
						requestedOptions);
			}
		} else if (supportsSX2) {
			AddDebugLogLineN(logKnownFiles,
				"Client which announced to support SX2 sent SX1 packet instead");
		}
		m_data.WriteHash(fileHash);
		m_data.WriteUInt16(0);
	}

	uint8 GetVersion() const { return m_version; }

	// Preserve the existing limit of 501 sources, checked after appending a source.
	bool IsFull() const { return m_count > 500; }

	// A source supplies the peer fields through the CUpDownClient accessors. Keeping this
	// header-only lets packet tests exercise the wire format without the application singleton.
	template <typename Source> void AddSource(uint32 id, const Source &source)
	{
		++m_count;
		m_data.WriteUInt32(id);
		m_data.WriteUInt16(source.GetUserPort());
		m_data.WriteUInt32(source.GetServerIP());
		m_data.WriteUInt16(source.GetServerPort());
		if (m_version >= 2) {
			m_data.WriteHash(source.GetUserHash());
		}
		if (m_version >= 4) {
			// Bits 0..2: crypt layer supported, requested, required; upper bits reserved.
			const uint8 options = (source.SupportsCryptLayer() ? 1 : 0) |
					      (source.RequestsCryptLayer() ? 2 : 0) |
					      (source.RequiresCryptLayer() ? 4 : 0);
			m_data.WriteUInt8(options);
		}
	}

	// The caller owns the result, as with CKnownFile::CreateSrcInfoPacket.
	CPacket *CreatePacket()
	{
		if (m_count == 0) {
			return nullptr;
		}
		m_data.Seek(m_sx2 ? 17 : 16, wxFromStart);
		m_data.WriteUInt16(m_count);
		auto *result =
			new CPacket(m_data, OP_EMULEPROT, m_sx2 ? OP_ANSWERSOURCES2 : OP_ANSWERSOURCES);
		if (result->GetPacketSize() > 354) {
			result->PackPacket();
		}
		return result;
	}

private:
	CMemFile m_data;
	const bool m_sx2;
	const uint8 m_version;
	uint16 m_count = 0;
};

#endif // SOURCEEXCHANGEPACKET_H
