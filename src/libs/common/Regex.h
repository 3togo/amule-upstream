// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef AMULE_COMMON_REGEX_H
#define AMULE_COMMON_REGEX_H

#include <wx/string.h>
#include <cstddef>
#include <cstdint>
#include <memory>

// Immutable, thread-safe compiled expression. Filtering policy belongs to callers.
// RE2 syntax, UTF-8, whole-text anchors and no dot/newline matching by default.
class CRegex
{
public:
	struct Options
	{
		bool caseSensitive = true;
		std::size_t maxPatternBytes = 4096;
		std::int64_t maxMemoryBytes = 8 * 1024 * 1024;
	};
	explicit CRegex(const wxString &pattern);
	CRegex(const wxString &pattern, const Options &options);
	~CRegex();
	CRegex(const CRegex &) = delete;
	CRegex &operator=(const CRegex &) = delete;
	bool IsValid() const;
	const wxString &GetError() const;
	bool Search(const wxString &text) const;
	bool FullMatch(const wxString &text) const;
	bool MatchesEmpty() const;

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};
#endif
