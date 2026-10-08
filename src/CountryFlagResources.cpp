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

#include "CountryFlagResources.h"
#include <config.h>
#include <wx/dir.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <vector>

wxString CountryFlagResources::ResolveRoot()
{
	const wxFileName executable(wxStandardPaths::Get().GetExecutablePath());
	const wxString base = executable.GetPath();
	// Executable-relative paths keep portable installs and relocated prefixes working.
	// The build tree stages the same share/amule layout; never search the working directory.
	const std::vector<wxString> candidates = { base + "/../Resources/artwork/flags",
		base + "/artwork/flags",
		base + "/../share/amule/artwork/flags",
		base + "/../../share/amule/artwork/flags",
		wxString::FromUTF8(AMULE_FLAG_DIR) };
	for (const auto &candidate : candidates) {
		if (wxFileName::FileExists(candidate + "/unknown.png")) {
			return candidate;
		}
	}
	return wxString::FromUTF8(AMULE_FLAG_DIR);
}

const CountryFlagResources &CountryFlagResources::Default()
{
	static const CountryFlagResources resources;
	return resources;
}

bool CountryFlagResources::ValidCode(const wxString &code)
{
	return code == "unknown" ||
	       (code.length() == 2 && code[0] >= 'a' && code[0] <= 'z' && code[1] >= 'a' && code[1] <= 'z');
}

CountryFlagResources::CountryFlagResources(const wxString &root)
: m_root(root)
{
	if (!wxFileName::DirExists(root)) {
		return;
	}
	wxDir directory(root);
	if (!directory.IsOpened()) {
		return;
	}
	wxString name;
	bool more = directory.GetFirst(&name, "*.png", wxDIR_FILES);
	while (more) {
		const wxString code = name.BeforeLast('.');
		if (ValidCode(code)) {
			m_codes.insert(code);
		}
		more = directory.GetNext(&name);
	}
}

bool CountryFlagResources::Read(const wxString &code, bool svg, std::string &bytes) const
{
	bytes.clear();
	if (!ValidCode(code) || !m_codes.count(code)) {
		return false;
	}
	const wxString path = m_root + "/" + code + (svg ? ".svg" : ".png");
	// Missing SVGs are normal for legacy raster flags and incomplete installs.
	if (!wxFileName::FileExists(path)) {
		return false;
	}
	wxFFile file(path, "rb");
	if (!file.IsOpened()) {
		return false;
	}
	const auto length = file.Length();
	if (length <= 0 || length > 4 * 1024 * 1024) {
		return false;
	}
	bytes.resize(static_cast<size_t>(length));
	if (file.Read(&bytes[0], bytes.size()) != bytes.size()) {
		bytes.clear();
		return false;
	}
	return true;
}
