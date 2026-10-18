// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef AMULE_DOWNLOADURL_H
#define AMULE_DOWNLOADURL_H

#include <wx/string.h>

// For diagnostics only: requests and conditional-download identity must keep the
// original URL. Query strings, fragments and user info can contain credentials.
// Reject non-HTTP input and control characters rather than logging them verbatim.
inline wxString DownloadUrlForLog(const wxString &url)
{
	wxString safe = url.BeforeFirst('?').BeforeFirst('#');
	for (const auto ch : safe) {
		if (ch <= 0x20 || ch == 0x7f || ch == '\\') {
			return "[invalid URL]";
		}
	}
	const auto scheme = safe.find("://");
	if (scheme == wxString::npos ||
		(safe.Left(scheme).Lower() != "http" && safe.Left(scheme).Lower() != "https")) {
		return "[invalid URL]";
	}
	const auto start = scheme + 3;
	const auto slash = safe.find('/', start);
	const auto end = slash == wxString::npos ? safe.length() : slash;
	const auto at = safe.Left(end).rfind('@');
	if (at != wxString::npos && at >= start) {
		safe.erase(start, at - start + 1);
	}
	if (safe.length() == start || safe[start] == '/') {
		return "[invalid URL]";
	}
	// A query delimiter inside malformed user info can leave "user:password"
	// looking like a host and port. Reject nonnumeric ports instead of echoing
	// those credentials. Inspect bracketed literals ourselves: older wxURI
	// versions reject valid compressed IPv6 addresses such as [::1].
	const wxString authority = safe.Mid(start).BeforeFirst('/');
	auto port = authority.find(':');
	if (authority.StartsWith("[")) {
		const auto close = authority.find(']');
		if (close == wxString::npos || !authority.Left(close).Contains(':')) {
			return "[invalid URL]";
		}
		const wxString literal = authority.Mid(1, close - 1);
		const auto zone = literal.find('%');
		if (zone != wxString::npos) {
			if (zone + 1 == literal.length()) {
				return "[invalid URL]";
			}
			for (const auto ch : literal.Mid(zone + 1)) {
				if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') ||
					    (ch >= 'A' && ch <= 'Z') || ch == '-' || ch == '.' || ch == '_' ||
					    ch == '~' || ch == '%')) {
					return "[invalid URL]";
				}
			}
		}
		for (const auto ch : literal.BeforeFirst('%')) {
			if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
				    (ch >= 'A' && ch <= 'F') || ch == ':' || ch == '.')) {
				return "[invalid URL]";
			}
		}
		port = close + 1;
		if (port == authority.length()) {
			return safe;
		}
		if (authority[port] != ':') {
			return "[invalid URL]";
		}
	} else if (port == 0 || authority.Contains('[') || authority.Contains(']')) {
		return "[invalid URL]";
	}
	if (port != wxString::npos) {
		const wxString number = authority.Mid(port + 1);
		if (number.IsEmpty()) {
			return "[invalid URL]";
		}
		for (const auto ch : number) {
			if (ch < '0' || ch > '9') {
				return "[invalid URL]";
			}
		}
	}
	return safe;
}

#endif
