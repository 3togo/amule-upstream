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

#include "Regex.h"
#include <re2/re2.h>
#include <string>

struct CRegex::Impl
{
	std::unique_ptr<re2::RE2> regex;
	wxString error;
};

CRegex::CRegex(const wxString &pattern)
: CRegex(pattern, Options{})
{
}
CRegex::CRegex(const wxString &pattern, const Options &options)
: m_impl(std::make_unique<Impl>())
{
	const auto bytes = pattern.utf8_str();
	if (bytes.length() > options.maxPatternBytes || options.maxMemoryBytes <= 0) {
		m_impl->error = "Regular expression exceeds the configured resource limits.";
		return;
	}
	re2::RE2::Options engineOptions;
	engineOptions.set_case_sensitive(options.caseSensitive);
	engineOptions.set_log_errors(false);
	engineOptions.set_max_mem(options.maxMemoryBytes);
	m_impl->regex =
		std::make_unique<re2::RE2>(re2::StringPiece(bytes.data(), bytes.length()), engineOptions);
	if (!m_impl->regex->ok()) {
		m_impl->error = wxString::FromUTF8(m_impl->regex->error());
		m_impl->regex.reset();
	}
}
CRegex::~CRegex() = default;
bool CRegex::IsValid() const
{
	return m_impl->regex != nullptr;
}
const wxString &CRegex::GetError() const
{
	return m_impl->error;
}
bool CRegex::Search(const wxString &text) const
{
	if (!IsValid())
		return false;
	const auto bytes = text.utf8_str();
	return re2::RE2::PartialMatch(re2::StringPiece(bytes.data(), bytes.length()), *m_impl->regex);
}
bool CRegex::FullMatch(const wxString &text) const
{
	if (!IsValid())
		return false;
	const auto bytes = text.utf8_str();
	return re2::RE2::FullMatch(re2::StringPiece(bytes.data(), bytes.length()), *m_impl->regex);
}
bool CRegex::MatchesEmpty() const
{
	return Search(wxEmptyString);
}
