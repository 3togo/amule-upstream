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

#include "GuiTestDiagnostics.h"
#include "SearchMetadataPanel.h"
#include <ec/cpp/SearchMetadata.h>
#include <wx/wx.h>
#include <wx/evtloop.h>
#include <wx/dcclient.h>
#include <wx/dcmemory.h>
#include <iostream>
#include <stdexcept>

class SearchMetadataTestApp : public wxApp
{
public:
	bool OnInit() override { return true; }
};
wxIMPLEMENT_APP_NO_MAIN(SearchMetadataTestApp);

namespace
{
void Check(bool condition, const char *message)
{
	if (!condition)
		throw std::runtime_error(message);
}
wxTextCtrl *Input(CSearchMetadataPanel &panel, const wxString &name)
{
	auto *field = dynamic_cast<wxTextCtrl *>(wxWindow::FindWindowByName(name, &panel));
	Check(field != nullptr, "Missing accessible metadata input");
	return field;
}
void TestFields(const wxString &screenshot)
{
	wxEventLoop loop;
	wxEventLoopActivator active(&loop);
	wxDialog dialog(nullptr,
		wxID_ANY,
		"Advanced media search",
		wxDefaultPosition,
		wxDefaultSize,
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
	auto *panel = new CSearchMetadataPanel(&dialog);
	auto *layout = new wxBoxSizer(wxVERTICAL);
	layout->Add(panel, wxSizerFlags(1).Expand().Border(wxALL, 12));
	dialog.SetSizerAndFit(layout);
	dialog.SetSize(dialog.FromDIP(wxSize(850, 250)));
	dialog.Show();
	wxYield();
	CSearchMetadata value;
	wxString error;
	wxWindow *invalid = nullptr;
	Check(panel->Read(value, error, invalid) && value.Empty(), "Empty controls impose a filter");
	Check(!panel->HasValues(), "Empty controls enable Reset");
	Input(*panel, _("Artist"))->SetValue(wxString::FromUTF8("演奏者"));
	Input(*panel, _("Album"))->SetValue("Test Album");
	Input(*panel, _("Title"))->SetValue("OR AND NOT");
	Input(*panel, _("Codec"))->SetValue("FLAC");
	Input(*panel, _("Minimum duration"))->SetValue("1:02:03");
	Input(*panel, _("Minimum bitrate (kbps)"))->SetValue("320");
	Check(panel->Read(value, error, invalid), "Valid fields rejected");
	Check(value.minLength == 3723 && value.minBitrateKbps == 320 &&
			value.artist == wxString::FromUTF8("演奏者") && value.album == "Test Album" &&
			value.title == "OR AND NOT" && value.codec == "FLAC",
		"Metadata fields mapped incorrectly");
	Check(panel->HasValues(), "Entered fields do not enable Reset");
	// Hiding advanced parameters must not erase the user's edits.
	panel->Hide();
	panel->Show();
	Check(panel->Read(value, error, invalid) && value.minLength == 3723, "Hide/show lost filters");
	for (const auto &name : { _("Artist"),
		     _("Album"),
		     _("Title"),
		     _("Codec"),
		     _("Minimum duration"),
		     _("Minimum bitrate (kbps)") }) {
		const wxRect rect = Input(*panel, name)->GetRect();
		Check(panel->GetClientRect().Contains(rect), "An input is clipped by the panel");
	}
	if (!screenshot.empty()) {
		wxInitAllImageHandlers();
		panel->Update();
		wxClientDC source(panel);
		wxBitmap bitmap(panel->GetClientSize());
		wxMemoryDC destination(bitmap);
		destination.Blit(0, 0, bitmap.GetWidth(), bitmap.GetHeight(), &source, 0, 0);
		destination.SelectObject(wxNullBitmap);
		Check(bitmap.SaveFile(screenshot, wxBITMAP_TYPE_PNG), "Could not save panel screenshot");
	}
	for (const auto &text : { "-1", "1.5", "320kbps", "1000001", "99999999999999999999999999" }) {
		Input(*panel, _("Minimum bitrate (kbps)"))->SetValue(text);
		Check(!panel->Read(value, error, invalid) &&
				invalid == Input(*panel, _("Minimum bitrate (kbps)")) && !error.empty(),
			"Invalid bitrate accepted or no field identified");
		Check(value.minBitrateKbps == 320, "Rejected input changed the previous metadata");
	}
	Input(*panel, _("Minimum bitrate (kbps)"))->SetValue("1000000");
	for (const auto &text : { "-1", "1.5", "1:02", "1:60:00", "24:00:01", "86401" }) {
		Input(*panel, _("Minimum duration"))->SetValue(text);
		Check(!panel->Read(value, error, invalid) &&
				invalid == Input(*panel, _("Minimum duration")) && !error.empty(),
			"Invalid duration accepted or no field identified");
	}
	for (const auto &text : { "86400", "24:00:00" }) {
		Input(*panel, _("Minimum duration"))->SetValue(text);
		Check(panel->Read(value, error, invalid) && value.minLength == 86400 &&
				value.minBitrateKbps == 1000000,
			"Inclusive bounds rejected");
	}
	panel->SetSupported(false);
	Check(!Input(*panel, _("Artist"))->IsEnabled() &&
			!Input(*panel, _("Minimum bitrate (kbps)"))->IsEnabled(),
		"Old-daemon fields remain editable");
	panel->SetSupported(true);
	Check(Input(*panel, _("Artist"))->IsEnabled() && panel->HasValues(),
		"Capability refresh lost field values");
	panel->Reset();
	Check(!panel->HasValues() && panel->Read(value, error, invalid) && value.Empty(),
		"Reset did not clear all six fields");
}
} // namespace

int main(int argc, char **argv)
{
	const wxString screenshot = argc > 1 ? wxString::FromUTF8(argv[1]) : wxString();
	InstallGuiTestDiagnostics();
	if (!wxEntryStart(argc, argv))
		return 77;
	RestoreGuiTestLog();
	int result = 0;
	if (!wxTheApp->CallOnInit())
		result = 1;
	else {
		try {
			TestFields(screenshot);
			std::cout << "PASS: metadata fields, units, validation, reset, visibility, layout "
				     "and daemon capability\n";
		} catch (const std::exception &error) {
			std::cerr << error.what() << '\n';
			result = 1;
		}
		wxTheApp->OnExit();
	}
	wxEntryCleanup();
	return result;
}
