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

#ifndef COUNTRYFLAGRESOURCES_H
#define COUNTRYFLAGRESOURCES_H

#include <set>
#include <string>
#include <wx/string.h>

// Shared, architecture-independent flag files. No GUI dependency or embedded artwork.
class CountryFlagResources
{
public:
	explicit CountryFlagResources(const wxString &root = ResolveRoot());
	static wxString ResolveRoot();
	static const CountryFlagResources &Default();
	static bool ValidCode(const wxString &code);
	const std::set<wxString> &Codes() const { return m_codes; }
	bool Read(const wxString &code, bool svg, std::string &bytes) const;

private:
	wxString m_root;
	std::set<wxString> m_codes;
};

#endif
