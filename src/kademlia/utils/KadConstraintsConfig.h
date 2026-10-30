// This file is part of the aMule Project.
// Copyright (c) 2003-2026 aMule Team ( https://amule-org.github.io )
// Licensed under the GNU General Public License, version 2 or later.
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
