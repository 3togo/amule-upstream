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

#ifndef AMULE_CONTACTDISTRIBUTION_H
#define AMULE_CONTACTDISTRIBUTION_H
#include <array>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <set>
namespace Kademlia
{
struct ContactDistribution
{
	static constexpr size_t BinCount = 64; // six most significant KadID bits
	std::array<uint32_t, BinCount> contacts{};
	std::array<uint32_t, BinCount> verified{};
	uint32_t subnets = 0;
	uint32_t Total() const
	{
		uint32_t sum = 0;
		for (auto n : contacts) {
			sum += n;
		}
		return sum;
	}
	uint32_t Verified() const
	{
		uint32_t sum = 0;
		for (auto n : verified) {
			sum += n;
		}
		return sum;
	}
	// EC payload v1: version byte, 64 (contacts, verified) big-endian uint32
	// pairs, then number of distinct /24s. No native struct layout on the wire.
	using Wire = std::array<uint8_t, 1 + BinCount * 8 + 4>;
	Wire Encode() const
	{
		Wire wire{};
		wire[0] = 1;
		size_t offset = 1;
		auto put = [&](uint32_t n) {
			for (int shift = 24; shift >= 0; shift -= 8) {
				wire[offset++] = static_cast<uint8_t>(n >> shift);
			}
		};
		for (size_t i = 0; i < BinCount; ++i) {
			put(contacts[i]);
			put(verified[i]);
		}
		put(subnets);
		return wire;
	}
	static bool Decode(const void *data, size_t size, ContactDistribution &out)
	{
		if (!data || size != Wire{}.size()) {
			return false;
		}
		const auto *wire = static_cast<const uint8_t *>(data);
		if (wire[0] != 1) {
			return false;
		}
		ContactDistribution decoded;
		size_t offset = 1;
		uint64_t total = 0;
		auto get = [&]() {
			uint32_t n = 0;
			for (int i = 0; i < 4; ++i) {
				n = (n << 8) | wire[offset++];
			}
			return n;
		};
		for (size_t i = 0; i < BinCount; ++i) {
			decoded.contacts[i] = get();
			decoded.verified[i] = get();
			if (decoded.verified[i] > decoded.contacts[i]) {
				return false;
			}
			total += decoded.contacts[i];
		}
		decoded.subnets = get();
		if (total > std::numeric_limits<uint32_t>::max() || decoded.subnets > total) {
			return false;
		}
		out = decoded;
		return true;
	}
};
class ContactDistributionBuilder
{
public:
	void Add(uint32_t mostSignificantIDWord, uint32_t kadIP, bool verified)
	{
		const size_t bin = mostSignificantIDWord >> 26;
		++m_data.contacts[bin];
		if (verified) {
			++m_data.verified[bin];
		}
		// Kad addresses are host order: mask the last octet, independent of the
		// host's memory byte order. Each IP prefix contributes one distinct /24.
		m_subnets.insert(kadIP & 0xffffff00);
	}
	ContactDistribution Get() const
	{
		auto data = m_data;
		data.subnets = m_subnets.size();
		return data;
	}

private:
	ContactDistribution m_data;
	std::set<uint32_t> m_subnets;
};
} // namespace Kademlia
#endif
