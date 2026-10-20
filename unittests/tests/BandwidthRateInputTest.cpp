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

#include <wx/wx.h>
#include <wx/numformatter.h>

#include "BandwidthRateConversion.h"
#include "BandwidthRateInput.h"

#include <iostream>
#include <stdexcept>

class RateInputTestApp : public wxApp
{
public:
	bool OnInit() override { return true; }
};
wxIMPLEMENT_APP_NO_MAIN(RateInputTestApp);

namespace
{
void Check(bool condition, const char *message)
{
	if (!condition) {
		throw std::runtime_error(message);
	}
}
void TestInput()
{
	using namespace BandwidthRateConversion;
	constexpr int maximum = 1000000;
	// Match the generic wxSpinCtrlDouble formatting used on Windows and macOS.
	const auto checkRate = [](int rate) {
		const wxString text = wxNumberFormatter::ToString(ToMegabits(rate), 4);
		Check(FromMegabits(ReadMegabits(text, -1), maximum) == rate,
			"Localized four-digit text must preserve the sampled limit");
	};
	// Cover both edges densely and sample the full range without a slow locale sweep.
	for (int rate = 0; rate <= 2048; ++rate) {
		checkRate(rate);
	}
	for (int rate = 0; rate <= maximum; rate += 997) {
		checkRate(rate);
	}
	for (int rate = maximum - 2048; rate <= maximum; ++rate) {
		checkRate(rate);
	}
	for (int rate : { 10, 1280, 125000 }) {
		checkRate(rate);
	}
	for (const wxString &text : { wxString(),
		     wxString("invalid"),
		     wxString("12invalid"),
		     wxString("nan"),
		     wxString("inf"),
		     wxString("-1") }) {
		Check(ReadMegabits(text, 10.5) == 10.5, "Invalid text must preserve the fallback");
	}
	wxDialog dialog(nullptr, wxID_ANY, "Rate input test");
	wxSpinCtrlDouble input(&dialog,
		wxID_ANY,
		wxEmptyString,
		wxDefaultPosition,
		wxDefaultSize,
		wxSP_ARROW_KEYS,
		0,
		ToMegabits(maximum),
		0,
		0.1);
	input.SetDigits(4);
	for (int rate : { 0, 10, 1280, 125000, maximum }) {
		input.SetValue(ToMegabits(rate));
		Check(FromMegabits(ReadMegabits(&input), maximum) == rate,
			"Accepting an untouched control must preserve its limit");
	}
	const wxString typed = wxNumberFormatter::ToString(ToMegabits(125000), 4);
	Check(FromMegabits(ReadMegabits(typed, 0), maximum) == 125000,
		"A text preview must parse a gigabit rate before focus loss");
}
} // namespace

int main(int argc, char **argv)
{
	if (!wxEntryStart(argc, argv)) {
		return 77;
	}
	int result = 0;
	if (!wxTheApp->CallOnInit()) {
		result = 1;
	} else {
		const int language =
			argc > 1 && wxString(argv[1]) == "it" ? wxLANGUAGE_ITALIAN : wxLANGUAGE_ENGLISH_US;
		wxLocale locale;
		if (!locale.Init(language, wxLOCALE_DONT_LOAD_DEFAULT)) {
			std::cerr << "Requested locale unavailable; skipping\n";
			result = 77;
		} else {
			try {
				Check(wxNumberFormatter::GetDecimalSeparator() ==
						(language == wxLANGUAGE_ITALIAN ? ',' : '.'),
					"Test must use the requested decimal separator");
				TestInput();
				std::cout << "Localized bandwidth input tests passed\n";
			} catch (const std::exception &error) {
				std::cerr << error.what() << '\n';
				result = 1;
			}
		}
		wxTheApp->OnExit();
	}
	wxEntryCleanup();
	return result;
}
