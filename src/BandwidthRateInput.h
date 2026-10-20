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

#ifndef AMULE_BANDWIDTH_RATE_INPUT_H
#define AMULE_BANDWIDTH_RATE_INPUT_H

#include <cmath>
#include <wx/numformatter.h>
#include <wx/spinctrl.h>

namespace BandwidthRateConversion
{
// Generic spin controls display localized grouping and decimal separators, and
// may not commit their text until focus loss. Never use a failed partial parse.
inline double ReadMegabits(const wxString &text, double fallback)
{
	double value;
	return wxNumberFormatter::FromString(text, &value) && std::isfinite(value) && value >= 0 ? value
												 : fallback;
}
inline double ReadMegabits(wxSpinCtrlDouble *input)
{
	return ReadMegabits(input->GetTextValue(), input->GetValue());
}
} // namespace BandwidthRateConversion
#endif
