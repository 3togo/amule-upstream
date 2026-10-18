// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later
#include <muleunit/test.h>
#include "DownloadUrl.h"

using namespace muleunit;
DECLARE_SIMPLE(DownloadUrl)

TEST(DownloadUrl, RemovesMaxMindLicenseAndCustomTokens)
{
	ASSERT_EQUALS(wxString("https://download.maxmind.com/app/geoip_download"),
		DownloadUrlForLog(
			"https://download.maxmind.com/app/"
			"geoip_download?edition_id=GeoLite2-Country&license_key=secret&suffix=tar.gz"));
	ASSERT_EQUALS(wxString("https://geo.example/db.mmdb"),
		DownloadUrlForLog("https://user:password@geo.example/db.mmdb?token=secret#fragment-secret"));
}

TEST(DownloadUrl, KeepsHostPortAndPath)
{
	ASSERT_EQUALS(wxString("HTTPS://[::1]:8443/a%20b/db.mmdb"),
		DownloadUrlForLog("HTTPS://user:p%40ss@[::1]:8443/a%20b/db.mmdb?token=secret"));
	ASSERT_EQUALS(
		wxString("http://example.com"), DownloadUrlForLog("http://user:pass@example.com?secret"));
	ASSERT_EQUALS(
		wxString("https://example.com/db.mmdb"), DownloadUrlForLog("https://example.com/db.mmdb"));
	ASSERT_EQUALS(wxString("https://example.com/db.mmdb"),
		DownloadUrlForLog("https://example.com/db.mmdb#secret?another-secret"));
}

TEST(DownloadUrl, DoesNotExposeUnsupportedOrUnsafeInput)
{
	for (const auto *url : { "",
		     "token=secret",
		     "ftp://user:secret@example.com/db?token=secret",
		     "https:///secret",
		     "https://user:secret@",
		     "https://example.com/\nsecret",
		     "https://example.com/\\secret" }) {
		ASSERT_EQUALS(wxString("[invalid URL]"), DownloadUrlForLog(url));
	}
	ASSERT_EQUALS(wxString("https://example.com/db.mmdb"),
		DownloadUrlForLog("https://user:first@second@example.com/db.mmdb?secret"));
}

TEST(DownloadUrl, RejectsMalformedCredentialBearingAuthorities)
{
	for (const auto *url : { "http://user:secret?token@host/db",
		     "http://user:secret#token@host/db",
		     "http://user:secret/path@host/db",
		     "https://example.com:not-a-port/db",
		     "https://[invalid-ipv6]/db" }) {
		ASSERT_EQUALS(wxString("[invalid URL]"), DownloadUrlForLog(url));
	}
}

TEST(DownloadUrl, DoesNotMistakeAtSignsInPathsOrQueriesForUserInfo)
{
	ASSERT_EQUALS(wxString("https://example.com/path@name/db"),
		DownloadUrlForLog("https://example.com/path@name/db?token=secret@other.example"));
	ASSERT_EQUALS(wxString("https://example.com/db"),
		DownloadUrlForLog("https://us%3Fer:p%23ss@example.com/db?token=secret"));
	ASSERT_EQUALS(wxString("[invalid URL]"),
		DownloadUrlForLog(wxString("https://example.com/db") + wxChar(0) + "secret"));
}

TEST(DownloadUrl, KeepsIPv6LiteralsAndScopeIdentifiers)
{
	ASSERT_EQUALS(wxString("https://[::1]/db"), DownloadUrlForLog("https://user:pass@[::1]/db?secret"));
	ASSERT_EQUALS(wxString("https://[fe80::1%25eth0]:8443/db"),
		DownloadUrlForLog("https://user:pass@[fe80::1%25eth0]:8443/db?secret"));
	ASSERT_EQUALS(wxString("[invalid URL]"), DownloadUrlForLog("https://[::1]:secret/db"));
	ASSERT_EQUALS(wxString("[invalid URL]"), DownloadUrlForLog("https://[::1]secret/db"));
}
