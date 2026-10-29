// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later
#include <muleunit/test.h>
#include "MagnetURI.h"
using namespace muleunit;
DECLARE_SIMPLE(MagnetImport)
namespace
{
const char *const ED2K_HASH = "d41d8cd98f00b204e9800998ecf8427e";
}

TEST(MagnetImport, StructuredImportRetainsMetadataAndOpaqueSourceHints)
{
	const auto result =
		CMagnetED2KConverter(STRING("magnet:?dn=hello%20world&xl=42&xt=urn:ed2khash:") + ED2K_HASH +
				     "&xs=ed2k-source&as=http%3A%2F%2Fexample.test%2Ffile&ws=web-seed")
			.Parse();
	ASSERT_TRUE(result.IsValid());
	ASSERT_EQUALS(STRING("hello%20world"), result.name);
	ASSERT_EQUALS(STRING("42"), result.size);
	ASSERT_EQUALS(STRING(ED2K_HASH), result.hash);
	ASSERT_EQUALS(size_t{ 3 }, result.sourceHints.size());
	ASSERT_EQUALS(STRING("ed2k-source"), result.sourceHints.front().uri);
	ASSERT_TRUE(result.sourceHints.front().kind == CMagnetED2KImport::SourceKind::Exact);
	ASSERT_TRUE(result.sourceHints.back().kind == CMagnetED2KImport::SourceKind::WebSeed);
	ASSERT_TRUE(result.GetED2KLink().find("source") == STRING::npos);
}
TEST(MagnetImport, StructuredImportExplainsUnsupportedIdentityAndMissingSize)
{
	const auto unsupported = CMagnetED2KConverter("magnet:?xt=urn:btih:abcd&xl=42").Parse();
	ASSERT_TRUE(unsupported.error == CMagnetED2KImport::Error::MissingED2KHash);
	ASSERT_TRUE(unsupported.GetED2KLink().empty());
	const auto noSize = CMagnetED2KConverter(STRING("magnet:?xt=urn:ed2k:") + ED2K_HASH).Parse();
	ASSERT_TRUE(noSize.error == CMagnetED2KImport::Error::MissingSize);
	ASSERT_TRUE(STRING(noSize.GetErrorMessage()).find("xl") != STRING::npos);
}
TEST(MagnetImport, StructuredImportRejectsMalformedIdentityAndLength)
{
	for (const auto *hash : { "short", "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz" }) {
		const auto result = CMagnetED2KConverter(STRING("magnet:?xl=42&xt=urn:ed2k:") + hash).Parse();
		ASSERT_TRUE(result.error == CMagnetED2KImport::Error::InvalidED2KHash);
		ASSERT_TRUE(result.GetED2KLink().empty());
	}
	for (const auto *size : { "", "0", "-1", "42junk", "18446744073709551616" }) {
		const auto result =
			CMagnetED2KConverter(STRING("magnet:?xt=urn:ed2k:") + ED2K_HASH + "&xl=" + size)
				.Parse();
		ASSERT_TRUE(result.error == CMagnetED2KImport::Error::InvalidSize);
		ASSERT_TRUE(result.GetED2KLink().empty());
	}
	const auto largest =
		CMagnetED2KConverter(STRING("magnet:?xt=urn:ed2k:") + ED2K_HASH + "&xl=18446744073709551615")
			.Parse();
	ASSERT_TRUE(largest.IsValid());
}

TEST(MagnetImport, BothModesKeepAichAndLegacyConverterBehavior)
{
	const STRING magnet = STRING("MAGNET:?xt=URN:ED2K:") + ED2K_HASH +
			      "&xl=42&xt=urn:aich:qvvwmk4s7zjuv3aszlpfyu4a5woaaaaa";
	const CMagnetED2KConverter converter(magnet);
	const auto result = converter.Parse();
	ASSERT_TRUE(result.IsValid());
	ASSERT_TRUE(result.aich == "QVVWMK4S7ZJUV3ASZLPFYU4A5WOAAAAA");
	ASSERT_TRUE(result.name == "FileName.ext");
	ASSERT_TRUE(result.GetED2KLink() == converter.GetED2KLink());
	ASSERT_TRUE(converter.CanConvertToED2K());
	const CMagnetED2KConverter invalid("magnet:?xt=urn:ed2k:bad&xl=42");
	ASSERT_FALSE(invalid.CanConvertToED2K());
	ASSERT_TRUE(invalid.GetED2KLink().empty());
}
