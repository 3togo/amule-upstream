// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later
#include <common/Regex.h>
#include <muleunit/test.h>
using namespace muleunit;
DECLARE_SIMPLE(Regex)

TEST(Regex, SearchAndAnchors)
{
	CRegex dog("dog.*");
	ASSERT_TRUE(dog.Search("hotdog"));
	ASSERT_TRUE(dog.Search("dog"));
	ASSERT_TRUE(!dog.FullMatch("hotdog"));
	ASSERT_TRUE(CRegex("^dog.*").Search("doghouse"));
	ASSERT_TRUE(!CRegex("^dog.*").Search("hotdog"));
	ASSERT_TRUE(CRegex("spam$").Search("this is spam"));
	ASSERT_TRUE(!CRegex("spam$").Search("spam alert"));
	ASSERT_TRUE(!CRegex("spam$").Search("spam\n"));
}
TEST(Regex, LiteralSpacesCommasAndEscapes)
{
	ASSERT_TRUE(CRegex("[0-9]{2,4}").FullMatch("123"));
	ASSERT_TRUE(CRegex("dog, cat").Search("a dog, cat pair"));
	ASSERT_TRUE(!CRegex(" dog ").Search("dog"));
	ASSERT_TRUE(CRegex("dog\\.").Search("dog."));
	ASSERT_TRUE(!CRegex("dog\\.").Search("dogs"));
}
TEST(Regex, CaseAndUnicode)
{
	CRegex::Options insensitive;
	insensitive.caseSensitive = false;
	ASSERT_TRUE(!CRegex("dog").Search("DOG"));
	ASSERT_TRUE(CRegex("dog", insensitive).Search("DOG"));
	ASSERT_TRUE(CRegex(wxString::FromUTF8("猫|犬")).Search(wxString::FromUTF8("小猫")));
	ASSERT_TRUE(CRegex(wxString::FromUTF8("ä"), insensitive).Search(wxString::FromUTF8("Ä")));
}
TEST(Regex, InvalidPatternsAndUnsupportedFeatures)
{
	for (const auto &pattern : { "(", "*", "(?=dog)", "(dog)\\1" }) {
		CRegex regex(pattern);
		ASSERT_TRUE(!regex.IsValid());
		ASSERT_TRUE(!regex.GetError().empty());
		ASSERT_TRUE(!regex.Search("dog"));
	}
}
TEST(Regex, EmptyMatchesAndNewlines)
{
	ASSERT_TRUE(CRegex("").MatchesEmpty());
	ASSERT_TRUE(CRegex("a*").MatchesEmpty());
	ASSERT_TRUE(!CRegex("dog.*").MatchesEmpty());
	ASSERT_TRUE(!CRegex("^dog.*$").Search("dog\ncat"));
	ASSERT_TRUE(!CRegex("^cat").Search("dog\ncat"));
}
TEST(Regex, LimitsAndLongInput)
{
	CRegex::Options limited;
	limited.maxPatternBytes = 3;
	ASSERT_TRUE(!CRegex("dogs", limited).IsValid());
	limited.maxMemoryBytes = 0;
	ASSERT_TRUE(!CRegex("dog", limited).IsValid());
	CRegex nested("^(a+)+$");
	ASSERT_TRUE(nested.IsValid());
	ASSERT_TRUE(!nested.Search(wxString('a', 200000) + "!"));
}
