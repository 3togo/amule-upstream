//
// This file is part of the aMule Project.
//
// Copyright (c) 2003-2026 aMule Team ( https://amule-org.github.io )
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
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
//

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
