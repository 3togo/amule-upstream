// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "FilterSelectionSummary.h"
#include <muleunit/test.h>

using namespace muleunit;
using namespace FilterSelectionSummary;

DECLARE_SIMPLE(FilterSelectionSummary)

TEST(FilterSelectionSummary, DisabledMasterIgnoresSavedRules)
{
	Settings settings;
	settings.allMessages = settings.nonFriends = settings.messageWords = true;
	settings.messageTerms = "saved phrase";
	const auto summary = Describe(settings);
	ASSERT_TRUE(summary.Contains("message filter is off"));
	ASSERT_TRUE(!summary.Contains("will be discarded"));
	ASSERT_TRUE(!summary.Contains("saved phrase"));
	ASSERT_EQUALS(wxString("saved phrase"), settings.messageTerms);
}

TEST(FilterSelectionSummary, AllMessagesOverridesSelectiveRules)
{
	Settings settings;
	settings.messages = settings.allMessages = settings.nonFriends = settings.messageWords = true;
	settings.messageTerms = "unused";
	settings.advancedSpam = settings.captcha = true;
	const auto summary = Describe(settings);
	ASSERT_TRUE(summary.Contains("All incoming messages outside current chats"));
	ASSERT_TRUE(!summary.Contains("friend list"));
	ASSERT_TRUE(!summary.Contains("unused"));
	ASSERT_TRUE(!summary.Contains("CAPTCHA"));
}

TEST(FilterSelectionSummary, EnabledMasterAloneAndEmptyTermsExplainNoEffect)
{
	Settings settings;
	settings.messages = true;
	ASSERT_TRUE(Describe(settings).Contains("select at least one option"));
	settings.messageWords = true;
	settings.messageTerms = " , ,  ";
	ASSERT_TRUE(Describe(settings).Contains("word list is empty"));
}

TEST(FilterSelectionSummary, SelectiveRulesUseParsedTermsAndExemptCurrentChats)
{
	Settings settings;
	settings.messages = settings.nonFriends = settings.unknownClients = settings.messageWords = true;
	settings.messageTerms = " Dog , , CAT , hot dog ";
	const auto summary = Describe(settings);
	ASSERT_TRUE(summary.Contains("friend list"));
	ASSERT_TRUE(summary.Contains("unknown clients"));
	ASSERT_TRUE(summary.Contains("ignoring case): Dog, CAT, hot dog."));
	ASSERT_TRUE(summary.Contains("Current chats are exempt"));
}

TEST(FilterSelectionSummary, OnlyExactMessageStarBlocksEverything)
{
	Settings settings;
	settings.messages = settings.messageWords = true;
	settings.messageTerms = "*";
	ASSERT_TRUE(Describe(settings).Contains("All incoming messages"));
	for (const auto &text : { " * ", "*,dog", "*," }) {
		settings.messageTerms = text;
		const auto summary = Describe(settings);
		ASSERT_TRUE(!summary.Contains("All incoming messages"));
		ASSERT_TRUE(summary.Contains("ignoring case): *"));
	}
}

TEST(FilterSelectionSummary, CommentTermsAreIndependentAndStarIsLiteral)
{
	Settings settings;
	settings.messages = settings.allMessages = settings.comments = true;
	settings.commentTerms = " * , Dog , , ";
	ASSERT_TRUE(Describe(settings).Contains("literal terms will be hidden (ignoring case): *, Dog."));
	settings.commentTerms = " , ";
	ASSERT_TRUE(Describe(settings).Contains("No file comments will be hidden"));
	settings.comments = false;
	ASSERT_TRUE(Describe(settings).Contains("File comment filtering is off"));
}

TEST(FilterSelectionSummary, AdvancedSpamIsIndependentButCaptchaNeedsItAndCoreSupport)
{
	Settings settings;
	settings.captcha = true;
	ASSERT_TRUE(!Describe(settings).Contains("CAPTCHA"));
	settings.advancedSpam = true;
	const auto summary = Describe(settings);
	ASSERT_TRUE(summary.Contains("message filter is off"));
	ASSERT_TRUE(summary.Contains("Advanced spam filtering"));
	ASSERT_TRUE(summary.Contains("require a CAPTCHA"));
	settings.advancedSpamSupported = false;
	ASSERT_TRUE(!Describe(settings).Contains("Advanced spam filtering"));
	ASSERT_TRUE(!Describe(settings).Contains("CAPTCHA"));
}

TEST(FilterSelectionSummary, ListsPreserveLiteralSpacesAndBackslashes)
{
	Settings settings;
	settings.comments = settings.logMessages = true;
	settings.commentTerms = "hot dog,C:\\spam";
	const auto summary = Describe(settings);
	ASSERT_TRUE(summary.Contains("hot dog, C:\\spam."));
	ASSERT_TRUE(summary.Contains("message text will also be written to the log"));
}

TEST(FilterSelectionSummary, RegexDoesNotUseLegacyStarOrCommaParsing)
{
	Settings settings;
	settings.messages = settings.messageWords = settings.messageRegex = true;
	settings.messageTerms = "*";
	settings.messagePattern = "dog.*|spam$";
	auto summary = Describe(settings);
	ASSERT_TRUE(summary.Contains("regular expression dog.*|spam$"));
	ASSERT_TRUE(!summary.Contains("All incoming messages"));
	settings.regexMatchesEmpty = true;
	ASSERT_TRUE(Describe(settings).Contains("can match every message"));
	settings.regexError = "missing parenthesis";
	ASSERT_TRUE(Describe(settings).Contains("Correct it before applying"));
	settings.regexSupported = false;
	ASSERT_TRUE(Describe(settings).Contains("connected core does not support"));
}
