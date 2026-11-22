// This file is part of the aMule Project.
// Copyright (c) 2003-2026 aMule Team ( https://amule-org.github.io )
// Licensed under the GNU General Public License, version 2 or later.

#include <muleunit/test.h>
#include "SourceExchangePacket.h"
#include <memory>
#include <vector>
#include <zlib.h>

using namespace muleunit;

namespace
{
using Bytes = std::vector<uint8>;
const uint8 HashBytes[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
const CMD4Hash Hash(HashBytes);

struct Source
{
	uint8 crypt = 0;
	uint16 GetUserPort() const { return 0x6655; }
	uint32 GetServerIP() const { return 0xaa998877; }
	uint16 GetServerPort() const { return 0xccbb; }
	const CMD4Hash &GetUserHash() const { return Hash; }
	bool SupportsCryptLayer() const { return (crypt & 1) != 0; }
	bool RequestsCryptLayer() const { return (crypt & 2) != 0; }
	bool RequiresCryptLayer() const { return (crypt & 4) != 0; }
};

Bytes Payload(const CPacket &packet)
{
	if (packet.GetProtocol() == OP_PACKEDPROT) {
		// Outgoing packets own a complete send buffer; UnPackPacket accepts received
		// packets only. Decode independently to check the actual compressed wire bytes.
		Bytes bytes(50000);
		uLongf size = bytes.size();
		ASSERT_EQUALS(Z_OK,
			uncompress(bytes.data(), &size, packet.GetDataBuffer(), packet.GetPacketSize()));
		bytes.resize(size);
		return bytes;
	}
	return Bytes(packet.GetDataBuffer(), packet.GetDataBuffer() + packet.GetPacketSize());
}

// Independent little-endian byte expectations; do not use the production stream writer.
Bytes Expected(uint8 version, bool sx2, uint8 crypt, unsigned count = 1)
{
	Bytes bytes;
	if (sx2) {
		bytes.push_back(version);
	}
	bytes.insert(bytes.end(), HashBytes, HashBytes + 16);
	bytes.push_back(count & 0xff);
	bytes.push_back(count >> 8);
	for (unsigned i = 0; i < count; ++i) {
		const Bytes record{ 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc };
		bytes.insert(bytes.end(), record.begin(), record.end());
		if (version >= 2) {
			bytes.insert(bytes.end(), HashBytes, HashBytes + 16);
		}
		if (version >= 4) {
			bytes.push_back(crypt);
		}
	}
	return bytes;
}
} // namespace

DECLARE_SIMPLE(SourceExchangePacket)

TEST(SourceExchangePacket, LegacyVersionsKeepTheirWireLayout)
{
	for (uint8 version : { 1, 2, 3, 4 }) {
		CSourceExchangePacket builder(Hash, version, false, 4, 0);
		ASSERT_EQUALS(version, builder.GetVersion());
		builder.AddSource(0x44332211, Source{ 5 });
		std::unique_ptr<CPacket> packet(builder.CreatePacket());
		ASSERT_TRUE(packet != nullptr);
		ASSERT_EQUALS(uint8(OP_ANSWERSOURCES), packet->GetOpCode());
		ASSERT_EQUALS(uint8(OP_EMULEPROT), packet->GetProtocol());
		ASSERT_TRUE(Payload(*packet) == Expected(version, false, 5));
	}
}

TEST(SourceExchangePacket, SX2NegotiatesVersionAndWritesItsPrefix)
{
	for (uint8 requested : { 1, 2, 3, 4, 5, 255 }) {
		const uint8 expectedVersion = requested > 4 ? 4 : requested;
		CSourceExchangePacket builder(Hash, 1, true, requested, 0xffff);
		ASSERT_EQUALS(expectedVersion, builder.GetVersion());
		builder.AddSource(0x44332211, Source{ 7 });
		std::unique_ptr<CPacket> packet(builder.CreatePacket());
		ASSERT_EQUALS(uint8(OP_ANSWERSOURCES2), packet->GetOpCode());
		ASSERT_TRUE(Payload(*packet) == Expected(expectedVersion, true, 7));
	}
}

TEST(SourceExchangePacket, SX2CapablePeerCanRequestLegacyPacket)
{
	CSourceExchangePacket builder(Hash, 2, true, 0, 0xffff);
	builder.AddSource(0x44332211, Source{});
	std::unique_ptr<CPacket> packet(builder.CreatePacket());
	ASSERT_EQUALS(uint8(2), builder.GetVersion());
	ASSERT_EQUALS(uint8(OP_ANSWERSOURCES), packet->GetOpCode());
	ASSERT_TRUE(Payload(*packet) == Expected(2, false, 0));
}

TEST(SourceExchangePacket, CryptFlagsUseOnlyTheirAssignedBits)
{
	for (uint8 crypt = 0; crypt < 8; ++crypt) {
		CSourceExchangePacket builder(Hash, 4, false, 0, 0);
		builder.AddSource(0x44332211, Source{ crypt });
		std::unique_ptr<CPacket> packet(builder.CreatePacket());
		ASSERT_TRUE(Payload(*packet) == Expected(4, false, crypt));
	}
}

TEST(SourceExchangePacket, EmptyResponseIsOmitted)
{
	for (bool sx2 : { false, true }) {
		CSourceExchangePacket builder(Hash, 4, sx2, 4, 0);
		ASSERT_FALSE(builder.IsFull());
		ASSERT_TRUE(builder.CreatePacket() == nullptr);
	}
}

TEST(SourceExchangePacket, SourceLimitAndPackedCountArePreserved)
{
	for (bool sx2 : { false, true }) {
		CSourceExchangePacket builder(Hash, 4, sx2, 4, 0);
		for (unsigned i = 0; i < 501; ++i) {
			ASSERT_FALSE(builder.IsFull());
			builder.AddSource(0x44332211, Source{ 3 });
		}
		ASSERT_TRUE(builder.IsFull());
		std::unique_ptr<CPacket> packet(builder.CreatePacket());
		ASSERT_EQUALS(uint8(OP_PACKEDPROT), packet->GetProtocol());
		ASSERT_TRUE(Payload(*packet) == Expected(4, sx2, 3, 501));
	}
}

TEST(SourceExchangePacket, CompressionThresholdKeepsSmallPacketsUnpacked)
{
	// SX1 v1: 18-byte header + 28*12-byte records = 354 bytes exactly.
	CSourceExchangePacket builder(Hash, 1, false, 0, 0);
	for (unsigned i = 0; i < 28; ++i) {
		builder.AddSource(0x44332211, Source{});
	}
	std::unique_ptr<CPacket> small(builder.CreatePacket());
	ASSERT_EQUALS(uint32(354), small->GetPacketSize());
	ASSERT_EQUALS(uint8(OP_EMULEPROT), small->GetProtocol());
	ASSERT_TRUE(Payload(*small) == Expected(1, false, 0, 28));
	// CreatePacket rewinds to patch the count; build the larger response independently.
	CSourceExchangePacket larger(Hash, 1, false, 0, 0);
	for (unsigned i = 0; i < 29; ++i) {
		larger.AddSource(0x44332211, Source{});
	}
	std::unique_ptr<CPacket> packed(larger.CreatePacket());
	ASSERT_EQUALS(uint8(OP_PACKEDPROT), packed->GetProtocol());
	ASSERT_TRUE(Payload(*packed) == Expected(1, false, 0, 29));
}
