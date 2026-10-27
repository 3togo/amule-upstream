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

#ifndef SEARCHMETADATA_H
#define SEARCHMETADATA_H

#include "ECTag.h"
#include <string>

// Shared by EC callers and the daemon. Keep values wide until validation so an
// oversized EC integer cannot wrap into an apparently valid filter.
struct CSearchMetadata
{
	static constexpr uint64_t MaxBitrateKbps = 1000000;
	static constexpr uint64_t MaxLengthSeconds = 24 * 60 * 60;
	uint64_t minBitrateKbps = 0;
	uint64_t minLength = 0;
	wxString codec;
	wxString title;
	wxString album;
	wxString artist;

	bool Empty() const
	{
		return minBitrateKbps == 0 && minLength == 0 && codec.empty() && title.empty() &&
		       album.empty() && artist.empty();
	}

	bool Validate(std::string &error) const
	{
		if (minBitrateKbps > MaxBitrateKbps) {
			error = "`min_bitrate_kbps` must be between 0 and 1000000";
			return false;
		}
		if (minLength > MaxLengthSeconds) {
			error = "`min_length` must be between 0 and 86400 seconds";
			return false;
		}
		for (const wxString *text : { &codec, &title, &album, &artist }) {
			if (text->find(wxChar(0)) != wxString::npos) {
				error = "Media search filters must not contain NUL characters";
				return false;
			}
		}
		error.clear();
		return true;
	}

	// Decimal seconds or h:mm:ss. Reject signs, whitespace, fractions, missing
	// components and overflow rather than silently dropping the filter.
	static bool ParseLength(const std::string &text, uint64_t &seconds)
	{
		uint64_t parts[3] = { 0, 0, 0 };
		size_t component = 0;
		size_t digits = 0;
		for (char c : text) {
			if (c == ':') {
				if (digits == 0 || component == 2 || (component > 0 && digits != 2))
					return false;
				++component;
				digits = 0;
			} else {
				if (c < '0' || c > '9' ||
					parts[component] > (MaxLengthSeconds - (c - '0')) / 10)
					return false;
				parts[component] = parts[component] * 10 + (c - '0');
				++digits;
			}
		}
		if (digits == 0)
			return false;
		uint64_t result = parts[0];
		if (component != 0) {
			if (component != 2 || digits != 2 || parts[1] > 59 || parts[2] > 59)
				return false;
			result = parts[0] * 3600 + parts[1] * 60 + parts[2];
		}
		if (result > MaxLengthSeconds)
			return false;
		seconds = result;
		return true;
	}

	void WriteTo(CECTag &tag, bool includeDefaults = false) const
	{
		if (minBitrateKbps || includeDefaults)
			tag.AddTag(CECTag(EC_TAG_SEARCH_MIN_BITRATE, minBitrateKbps));
		if (minLength || includeDefaults)
			tag.AddTag(CECTag(EC_TAG_SEARCH_MIN_LENGTH, minLength));
		if (!codec.empty() || includeDefaults)
			tag.AddTag(CECTag(EC_TAG_SEARCH_CODEC, codec));
		if (!title.empty() || includeDefaults)
			tag.AddTag(CECTag(EC_TAG_SEARCH_TITLE, title));
		if (!album.empty() || includeDefaults)
			tag.AddTag(CECTag(EC_TAG_SEARCH_ALBUM, album));
		if (!artist.empty() || includeDefaults)
			tag.AddTag(CECTag(EC_TAG_SEARCH_ARTIST, artist));
	}

	bool ReadFrom(const CECTag &tag, std::string &error)
	{
		CSearchMetadata value;
		unsigned seen = 0;
		for (const CECTag &child : tag) {
			uint64_t *number = nullptr;
			wxString *text = nullptr;
			unsigned bit = 0;
			switch (child.GetTagName()) {
			case EC_TAG_SEARCH_MIN_BITRATE:
				number = &value.minBitrateKbps;
				bit = 1;
				break;
			case EC_TAG_SEARCH_MIN_LENGTH:
				number = &value.minLength;
				bit = 2;
				break;
			case EC_TAG_SEARCH_CODEC:
				text = &value.codec;
				bit = 4;
				break;
			case EC_TAG_SEARCH_TITLE:
				text = &value.title;
				bit = 8;
				break;
			case EC_TAG_SEARCH_ALBUM:
				text = &value.album;
				bit = 16;
				break;
			case EC_TAG_SEARCH_ARTIST:
				text = &value.artist;
				bit = 32;
				break;
			default:
				continue;
			}
			if ((seen & bit) || (number && !child.IsInt()) || (text && !child.IsString())) {
				error = "Invalid or duplicate media search filter";
				return false;
			}
			seen |= bit;
			if (number)
				*number = child.GetInt();
			if (text)
				*text = child.GetStringData();
		}
		if (!value.Validate(error))
			return false;
		*this = value;
		return true;
	}
};

#endif // SEARCHMETADATA_H
