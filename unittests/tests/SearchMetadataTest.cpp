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

#include <muleunit/test.h>
#include <ec/cpp/ECSpecialTags.h>
#include <limits>

using namespace muleunit;
DECLARE_SIMPLE(SearchMetadata)

TEST(SearchMetadata, DurationGrammarAndBoundaries)
{
	for (const auto &text : { "0", "86400", "24:00:00", "0:00:00", "1:02:03", "3723" }) {
		uint64_t seconds = 999999;
		ASSERT_TRUE(CSearchMetadata::ParseLength(text, seconds));
		ASSERT_TRUE(seconds <= 86400);
	}
	uint64_t seconds = 0;
	ASSERT_TRUE(CSearchMetadata::ParseLength("1:02:03", seconds));
	ASSERT_EQUALS(uint64_t(3723), seconds);
	for (const auto &text : { "",
		     "-1",
		     "+1",
		     " 1",
		     "1 ",
		     "1.5",
		     "1:02",
		     "1:2:03",
		     "1:02:3",
		     "1:60:00",
		     "0:00:60",
		     "24:00:01",
		     "86401",
		     "9999999999999999999999999999999999999",
		     "1:02:03:04" }) {
		seconds = 123;
		ASSERT_TRUE(!CSearchMetadata::ParseLength(text, seconds));
		ASSERT_EQUALS(uint64_t(123), seconds);
	}
}

TEST(SearchMetadata, LegacyRequestsHaveNoNewTags)
{
	CEC_Search_Tag tag("linux", EC_SEARCH_KAD, "", "", 0, 0, 0);
	ASSERT_TRUE(tag.GetTagByName(EC_TAG_SEARCH_MIN_BITRATE) == nullptr);
	ASSERT_TRUE(tag.GetTagByName(EC_TAG_SEARCH_MIN_LENGTH) == nullptr);
	ASSERT_EQUALS(size_t(2), tag.GetTagCount());
	CSearchMetadata metadata;
	std::string error;
	ASSERT_TRUE(metadata.ReadFrom(tag, error));
	ASSERT_TRUE(metadata.Empty());
}

TEST(SearchMetadata, AllFieldsRoundTripAndBoundsAreInclusive)
{
	CSearchMetadata sent;
	sent.minBitrateKbps = CSearchMetadata::MaxBitrateKbps;
	sent.minLength = CSearchMetadata::MaxLengthSeconds;
	sent.codec = "FLAC";
	sent.title = wxString::FromUTF8("曲 AND OR NOT");
	sent.album = "Test Album";
	sent.artist = "Test Artist";
	CEC_Search_Tag tag("music", EC_SEARCH_KAD, "Audio", "flac", 1, 0, 0, sent);
	CSearchMetadata received;
	std::string error;
	ASSERT_TRUE(received.ReadFrom(tag, error));
	ASSERT_EQUALS(sent.minBitrateKbps, received.minBitrateKbps);
	ASSERT_EQUALS(sent.minLength, received.minLength);
	ASSERT_EQUALS(sent.codec, received.codec);
	ASSERT_EQUALS(sent.title, received.title);
	ASSERT_EQUALS(sent.album, received.album);
	ASSERT_EQUALS(sent.artist, received.artist);
}

TEST(SearchMetadata, RejectsWrongTypesDuplicatesAndWideIntegers)
{
	for (auto name : { EC_TAG_SEARCH_MIN_BITRATE, EC_TAG_SEARCH_MIN_LENGTH }) {
		for (uint64_t value :
			{ uint64_t(1000001), uint64_t(1) << 32, std::numeric_limits<uint64_t>::max() }) {
			CECTag tag(EC_TAG_SEARCH_TYPE, uint8_t(EC_SEARCH_KAD));
			tag.AddTag(CECTag(name, value));
			CSearchMetadata metadata;
			std::string error;
			ASSERT_TRUE(!metadata.ReadFrom(tag, error));
			ASSERT_TRUE(!error.empty());
		}
		CECTag tag(EC_TAG_SEARCH_TYPE, uint8_t(EC_SEARCH_KAD));
		tag.AddTag(CECTag(name, wxString("128")));
		CSearchMetadata metadata;
		std::string error;
		ASSERT_TRUE(!metadata.ReadFrom(tag, error));
	}
	for (auto name :
		{ EC_TAG_SEARCH_CODEC, EC_TAG_SEARCH_TITLE, EC_TAG_SEARCH_ALBUM, EC_TAG_SEARCH_ARTIST }) {
		CECTag tag(EC_TAG_SEARCH_TYPE, uint8_t(EC_SEARCH_KAD));
		tag.AddTag(CECTag(name, uint32_t(1)));
		CSearchMetadata metadata;
		std::string error;
		ASSERT_TRUE(!metadata.ReadFrom(tag, error));
	}
	CECTag duplicate(EC_TAG_SEARCH_TYPE, uint8_t(EC_SEARCH_KAD));
	duplicate.AddTag(CECTag(EC_TAG_SEARCH_TITLE, wxString("first")));
	duplicate.AddTag(CECTag(EC_TAG_SEARCH_TITLE, wxString("second")));
	CSearchMetadata metadata;
	std::string error;
	ASSERT_TRUE(!metadata.ReadFrom(duplicate, error));
}
