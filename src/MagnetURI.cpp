//                                                       -*- C++ -*-
// This file is part of the aMule Project.
//
// Copyright (c) 2003-2026 aMule Team ( https://amule-org.github.io )
// Copyright (c) 2007-2011 Dévai Tamás ( gonosztopi@amule.org )
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

#include "MagnetURI.h"
#include <cstdint>
#include <limits>

#ifdef USE_STD_STRING
#ifdef _T
#undef _T
#endif
#ifdef _C
#undef _C
#endif
#define _T(str) str
#define _C(ch) ch
#else
// wx/chartype.h defines _T
#define _C(ch) wxChar(ch)
#endif

namespace
{

unsigned CharCode(char c)
{
	return static_cast<unsigned char>(c);
}

#ifndef USE_STD_STRING
unsigned CharCode(const wxUniChar &c)
{
	return c.GetValue();
}
#endif

unsigned AsciiLower(unsigned c)
{
	return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

// Schemes and URN namespace names are case-insensitive (RFC 3986 section 3.1, RFC 8141).
// @a lowerPrefix is ASCII lower case.
bool StartsWithNoCase(const STRING &s, const char *lowerPrefix)
{
	for (size_t i = 0; lowerPrefix[i] != '\0'; ++i) {
		if (i >= s.length() ||
			AsciiLower(CharCode(s[i])) != static_cast<unsigned char>(lowerPrefix[i])) {
			return false;
		}
	}
	return true;
}

} // namespace

bool CMagnetURI::IsMagnet(const STRING &uri)
{
	return StartsWithNoCase(uri, "magnet:");
}

CMagnetURI::CMagnetURI(const STRING &uri)
{
	if (IsMagnet(uri)) {
		size_t start = uri.find(_C('?'));
		if (start == STRING::npos)
			start = uri.length();
		while (start < uri.length() - 1) {
			size_t end = uri.find(_C('&'), start + 1);
			if (end == STRING::npos)
				end = uri.length();
			size_t pos = uri.find(_C('='), start + 1);
			if (pos == STRING::npos)
				pos = uri.length();
			if (pos < end) {
				m_fields.push_back(Field_Type(uri.substr(start + 1, pos - start - 1),
					uri.substr(pos + 1, end - pos - 1)));
			}
			start = end;
		}
	}
}

STRING CMagnetURI::GetLink() const
{
	STRING retval(_T("magnet:"));

	for (List_Type::const_iterator it = m_fields.begin(); it != m_fields.end(); ++it) {
		if (it == m_fields.begin()) {
			retval.append(1, _C('?'));
		} else {
			retval.append(1, _C('&'));
		}
		retval.append(it->first);
		retval.append(1, _C('='));
		retval.append(it->second);
	}
	return retval;
}

CMagnetURI::Value_List CMagnetURI::GetField(const STRING &name) const
{
	Value_List retval;

	for (List_Type::const_iterator it = m_fields.begin(); it != m_fields.end(); ++it) {
		if (it->first.compare(name) == 0) {
			retval.push_back(it->second);
		}
	}
	return retval;
}

namespace
{

// A base32-encoded AICH master hash is always 32 characters from [A-Z2-7] (RFC 4648, no padding --
// 20 raw bytes need exactly ceil(160/5) = 32 symbols). ED2KLink.cpp's parser throws on anything
// else, so a magnet carrying a junk or truncated urn:aich: must have it dropped here rather than
// embedded -- otherwise a perfectly valid ed2k hash would be rejected wholesale just because the
// AICH tagalong was malformed. A plain char-range check, with no CAICHHash dependency, so it works
// in both build modes.
bool IsWellFormedAich(const STRING &s)
{
	if (s.length() != 32) {
		return false;
	}
	for (size_t i = 0; i < s.length(); ++i) {
		if (!((s[i] >= _C('A') && s[i] <= _C('Z')) || (s[i] >= _C('2') && s[i] <= _C('7')))) {
			return false;
		}
	}
	return true;
}

} // namespace

CMagnetED2KImport CMagnetED2KConverter::Parse() const
{
	CMagnetED2KImport result;
	const auto names = GetField(_T("dn"));
	result.name = names.empty() ? STRING(_T("FileName.ext")) : names.front();
	const auto sizes = GetField(_T("xl"));
	if (!sizes.empty()) {
		result.size = sizes.front();
	}
	for (const auto &urn : GetField(_T("xt"))) {
		if (result.hash.empty() && StartsWithNoCase(urn, "urn:ed2k:")) {
			result.hash = urn.substr(9);
		} else if (result.hash.empty() && StartsWithNoCase(urn, "urn:ed2khash:")) {
			result.hash = urn.substr(13);
		} else if (result.aich.empty() && StartsWithNoCase(urn, "urn:aich:")) {
			result.aich = urn.substr(9);
		}
	}
	for (const auto &field : m_fields) {
		if (field.first == _T("xs")) {
			result.sourceHints.push_back({ CMagnetED2KImport::SourceKind::Exact, field.second });
		} else if (field.first == _T("as")) {
			result.sourceHints.push_back(
				{ CMagnetED2KImport::SourceKind::Acceptable, field.second });
		} else if (field.first == _T("ws")) {
			result.sourceHints.push_back(
				{ CMagnetED2KImport::SourceKind::WebSeed, field.second });
		}
	}
	// Keep the established optional-AICH behavior: normalize and discard junk.
	const STRING &readAich = result.aich;
	for (size_t i = 0; i < result.aich.length(); ++i) {
		const unsigned c = CharCode(readAich[i]);
		if (c >= 'a' && c <= 'z') {
			result.aich[i] = static_cast<char>(c - ('a' - 'A'));
		}
	}
	if (!IsWellFormedAich(result.aich)) {
		result.aich.clear();
	}
	if (result.hash.empty()) {
		return result;
	}
	if (result.hash.length() != 32) {
		result.error = CMagnetED2KImport::Error::InvalidED2KHash;
		return result;
	}
	const STRING &readHash = result.hash;
	for (const auto c : readHash) {
		const unsigned code = AsciiLower(CharCode(c));
		if (!((code >= '0' && code <= '9') || (code >= 'a' && code <= 'f'))) {
			result.error = CMagnetED2KImport::Error::InvalidED2KHash;
			return result;
		}
	}
	if (sizes.empty()) {
		result.error = CMagnetED2KImport::Error::MissingSize;
		return result;
	}
	uint64_t size = 0;
	bool validSize = !result.size.empty();
	const STRING &readSize = result.size;
	for (const auto c : readSize) {
		const unsigned code = CharCode(c);
		if (code < '0' || code > '9' ||
			size > (std::numeric_limits<uint64_t>::max() - (code - '0')) / 10) {
			validSize = false;
			break;
		}
		size = size * 10 + (code - '0');
	}
	result.error = validSize && size != 0 ? CMagnetED2KImport::Error::None
					      : CMagnetED2KImport::Error::InvalidSize;
	return result;
}

const char *CMagnetED2KImport::GetErrorMessage() const
{
	switch (error) {
	case Error::None:
		return "";
	case Error::MissingED2KHash:
		return "missing supported eD2k exact-topic hash";
	case Error::InvalidED2KHash:
		return "eD2k hash must contain 32 hexadecimal digits";
	case Error::MissingSize:
		return "missing exact length (xl)";
	case Error::InvalidSize:
		return "exact length must be a positive 64-bit decimal integer";
	}
	return "invalid magnet";
}

STRING CMagnetED2KImport::GetED2KLink() const
{
	if (!IsValid()) {
		return STRING();
	}
	STRING link = STRING(_T("ed2k://|file|"))
			      .append(name)
			      .append(1, _C('|'))
			      .append(size)
			      .append(1, _C('|'))
			      .append(hash)
			      .append(1, _C('|'));
	if (!aich.empty()) {
		link.append(_T("h=")).append(aich).append(1, _C('|'));
	}
	return link.append(_T("/"));
}

bool CMagnetED2KConverter::CanConvertToED2K() const
{
	return Parse().IsValid();
}

STRING CMagnetED2KConverter::GetED2KLink() const
{
	return Parse().GetED2KLink();
}
