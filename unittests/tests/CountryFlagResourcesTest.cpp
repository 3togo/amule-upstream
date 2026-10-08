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
#include <wx/init.h>
#include <wx/filename.h>
#include <wx/ffile.h>
#include <iostream>
#include <stdexcept>

void require(bool condition, const char *message)
{
	if (!condition)
		throw std::runtime_error(message);
}

int main(int argc, char **argv)
{
	wxInitializer init;
	if (!init.IsOk() || argc > 2)
		return 1;
	try {
		CountryFlagResources resources(
			argc == 2 ? wxString::FromUTF8(argv[1]) : CountryFlagResources::ResolveRoot());
		require(resources.Codes().size() == 253, "incomplete shared artwork inventory");
		int vectors = 0;
		std::string bytes;
		for (const auto &code : resources.Codes()) {
			require(resources.Read(code, false, bytes), "PNG read failed");
			require(bytes.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) == 0, "invalid PNG bytes");
			if (resources.Read(code, true, bytes)) {
				++vectors;
				require(bytes.find("<svg") != std::string::npos, "invalid SVG bytes");
			}
		}
		require(vectors == 251, "incomplete vector artwork");
		for (const char *code : { "../us", "US", "u", "usa", "zz", "us.svg", "", "amule" }) {
			require(!resources.Read(wxString::FromUTF8(code), false, bytes) && bytes.empty(),
				"invalid country path accepted");
		}
		require(!resources.Read("unknown", true, bytes), "legacy SVG should be absent");
		CountryFlagResources missing("/nonexistent-amule-artwork");
		require(missing.Codes().empty() && !missing.Read("us", false, bytes),
			"missing data accepted");
		const wxString root = wxFileName::CreateTempFileName("amule-flags-");
		wxRemoveFile(root);
		require(wxFileName::Mkdir(root), "temporary directory failed");
		{
			wxFFile file(root + "/us.png", "wb");
			file.Write("original", 8);
		}
		CountryFlagResources temporary(root);
		require(temporary.Read("us", false, bytes) && bytes == "original", "temporary read failed");
		{
			wxFFile file(root + "/us.png", "wb");
			file.Write("updated", 7);
		}
		require(temporary.Read("us", false, bytes) && bytes == "updated",
			"replacement artwork stale");
		wxRemoveFile(root + "/us.png");
		require(!temporary.Read("us", false, bytes), "removed artwork stale");
		wxFileName::Rmdir(root);
		std::cout << "PASS: shared flag bytes, inventory, validation, updates and missing files\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << "\n";
		return 1;
	}
	return 0;
}
