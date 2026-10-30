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

#ifndef KADEMLIA_KADCONSTRAINTSCONFIG_H
#define KADEMLIA_KADCONSTRAINTSCONFIG_H
#include "KadConstraints.h"
#include <wx/config.h>

namespace Kademlia
{
inline SKadConstraints LoadKadConstraints(wxConfigBase &cfg, uint32_t connections, uint32_t sourceSearches)
{
	SKadConstraints result;
	for (const auto &setting : SKadConstraints::Settings()) {
		const wxString key = "/Kad/" + wxString::FromUTF8(setting.key);
		wxString text;
		wxULongLong_t value = 0;
		cfg.Read(key, &text);
		text.Trim(true).Trim(false);
		// Parse before narrowing: malformed, negative, zero-invalid and overflowing
		// values recover to the documented default instead of wrapping or disabling caps.
		if (!text.empty() && !text.StartsWith("-") && text.ToULongLong(&value) &&
			value >= setting.minimum && value <= setting.maximum) {
			result.*(setting.member) = static_cast<uint32_t>(value);
		} else {
			result.*(setting.member) = setting.fallback;
		}
	}
	result.Resolve(connections, sourceSearches);
	return result;
}
inline void SaveKadConstraints(wxConfigBase &cfg, const SKadConstraints &settings)
{
	for (const auto &setting : SKadConstraints::Settings()) {
		uint32_t value = settings.*(setting.member);
		if ((setting.member == &SKadConstraints::globalLookupActive && settings.automaticActive) ||
			(setting.member == &SKadConstraints::globalLookupPending &&
				settings.automaticPending)) {
			value = 0;
		}
		cfg.Write("/Kad/" + wxString::FromUTF8(setting.key), wxString::Format("%u", value));
	}
}
} // namespace Kademlia
#endif
