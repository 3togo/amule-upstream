//								-*- C++ -*-
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

#include <muleunit/test.h>
#include <kademlia/utils/ContactDistribution.h>
using namespace muleunit;
using Kademlia::ContactDistribution;
using Kademlia::ContactDistributionBuilder;
DECLARE_SIMPLE(ContactDistribution)
TEST(ContactDistribution, PrefixBoundariesAndSubnetDeduplication)
{
	ContactDistributionBuilder builder;
	builder.Add(0, 0x01020304, false);
	builder.Add(0x03ffffff, 0x010203ff, true);
	builder.Add(0x04000000, 0x01020401, true);
	builder.Add(0xffffffff, 0x09080706, false);
	auto data = builder.Get();
	ASSERT_EQUALS(2u, data.contacts[0]);
	ASSERT_EQUALS(1u, data.contacts[1]);
	ASSERT_EQUALS(1u, data.contacts[63]);
	ASSERT_EQUALS(4u, data.Total());
	ASSERT_EQUALS(2u, data.Verified());
	ASSERT_EQUALS(3u, data.subnets);
}
TEST(ContactDistribution, PortableWireAndSnapshot)
{
	ContactDistributionBuilder builder;
	builder.Add(0, 0x01020304, true);
	auto data = builder.Get();
	const auto wire = data.Encode();
	ASSERT_EQUALS(size_t(517), wire.size());
	ASSERT_EQUALS(uint8_t(1), wire[0]);
	ASSERT_EQUALS(uint8_t(0), wire[1]);
	ASSERT_EQUALS(uint8_t(1), wire[4]);
	ASSERT_EQUALS(uint8_t(1), wire[8]);
	ASSERT_EQUALS(uint8_t(1), wire[516]);
	ContactDistribution decoded;
	ASSERT_TRUE(ContactDistribution::Decode(wire.data(), wire.size(), decoded));
	ASSERT_EQUALS(1u, decoded.Total());
	builder.Add(0xffffffff, 0x01020401, false);
	ASSERT_EQUALS(1u, data.Total());
	ASSERT_EQUALS(2u, builder.Get().Total());
}
TEST(ContactDistribution, RejectMalformedWithoutOverwritingSnapshot)
{
	ContactDistribution data;
	data.contacts[0] = 1;
	data.verified[0] = 1;
	data.subnets = 1;
	auto wire = data.Encode();
	ContactDistribution out = data;
	ASSERT_FALSE(ContactDistribution::Decode(nullptr, wire.size(), out));
	ASSERT_FALSE(ContactDistribution::Decode(wire.data(), 516, out));
	wire[0] = 2;
	ASSERT_FALSE(ContactDistribution::Decode(wire.data(), wire.size(), out));
	wire[0] = 1;
	wire[8] = 2;
	ASSERT_FALSE(ContactDistribution::Decode(wire.data(), wire.size(), out));
	wire = data.Encode();
	wire[516] = 2;
	ASSERT_FALSE(ContactDistribution::Decode(wire.data(), wire.size(), out));
	ASSERT_EQUALS(1u, out.Total());
	ContactDistribution empty;
	wire = empty.Encode();
	ASSERT_TRUE(ContactDistribution::Decode(wire.data(), wire.size(), out));
	ASSERT_EQUALS(0u, out.Total());
}
