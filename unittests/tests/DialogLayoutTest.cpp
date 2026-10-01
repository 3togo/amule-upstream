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

#include "DialogLayout.h"

#include <iostream>
#include <stdexcept>

class DialogLayoutTestApp : public wxApp
{
public:
	bool OnInit() override { return true; }
};

wxIMPLEMENT_APP_NO_MAIN(DialogLayoutTestApp);

namespace
{
void Check(bool condition, const char *message)
{
	if (!condition) {
		throw std::runtime_error(message);
	}
}

void TestScrollableContent()
{
	wxDialog dialog(nullptr,
		wxID_ANY,
		"Dialog layout test",
		wxDefaultPosition,
		wxDefaultSize,
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
	wxScrolledWindow *content = new wxScrolledWindow(&dialog);
	content->SetMinSize(dialog.FromDIP(wxSize(240, 200)));
	ConfigureDialogScrolling(content);

	wxBoxSizer *fields = new wxBoxSizer(wxVERTICAL);
	wxStaticText *label = new wxStaticText(content, wxID_ANY, wxString('W', 200));
	fields->Add(label);
	wxPanel *section = new wxPanel(content);
	section->SetMinSize(dialog.FromDIP(wxSize(1200, 1200)));
	fields->Add(section);
	content->SetSizer(fields);

	wxButton *button = new wxButton(&dialog, wxID_OK);
	wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);
	top->Add(content, wxSizerFlags(1).Expand());
	top->Add(button, wxSizerFlags().Right().Border(wxALL, 5));
	dialog.SetSizer(top);
	FitScrollableDialog(&dialog, content);

	const wxSize available = wxDisplay(&dialog).GetClientArea().GetSize();
	Check(dialog.GetSize().x <= available.x && dialog.GetSize().y <= available.y,
		"Initial dialog extends beyond the display work area");

	const wxSize compact = dialog.FromDIP(wxSize(640, 480));
	dialog.SetSize(compact);
	dialog.Layout();
	content->FitInside();
	Check(dialog.GetSize() == compact, "Content prevents shrinking the dialog");
	Check(content->GetVirtualSize().x > content->GetClientSize().x &&
			content->GetVirtualSize().y > content->GetClientSize().y,
		"Oversized content is clipped instead of scrollable");
	Check(button->GetParent() == &dialog && button->GetRect().GetBottom() <= dialog.GetClientSize().y &&
			button->GetRect().GetRight() <= dialog.GetClientSize().x,
		"Action button is outside the visible dialog");

	content->Scroll(10, 10);
	int x = 0;
	int y = 0;
	content->GetViewStart(&x, &y);
	Check(x > 0 && y > 0, "Both scroll directions must work");

	// File Details changes both section visibility and label lengths on updates.
	section->Hide();
	label->SetLabel("Short name");
	content->FitInside();
	dialog.Layout();
	Check(dialog.GetSize() == compact, "Hiding a section resized the dialog");
	Check(content->GetVirtualSize().x <= content->GetClientSize().x &&
			content->GetVirtualSize().y <= content->GetClientSize().y,
		"Virtual size did not shrink after content was removed");

	section->Show();
	content->FitInside();
	dialog.Layout();
	Check(dialog.GetSize() == compact, "Showing a section resized the dialog");
	Check(content->GetVirtualSize().y >= fields->GetMinSize().y,
		"Newly shown content cannot be reached by scrolling");
}

void TestHorizontalScrollEdge()
{
	wxDialog dialog(nullptr, wxID_ANY, "Horizontal scroll edge test");
	wxScrolledWindow *content = new wxScrolledWindow(&dialog);
	ConfigureDialogScrolling(content);
	wxBoxSizer *fields = new wxBoxSizer(wxVERTICAL);
	wxPanel *section = new wxPanel(content);
	section->SetMinSize(wxSize(1003, 100));
	fields->Add(section, wxSizerFlags().Expand());
	content->SetSizer(fields);
	wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);
	top->Add(content, wxSizerFlags(1).Expand());
	dialog.SetSizer(top);

	// Exercise every remainder of the old ten-pixel horizontal scroll unit.
	for (int width = 313; width < 323; ++width) {
		dialog.SetClientSize(wxSize(width, 300));
		dialog.Layout();
		content->FitInside();
		content->Scroll(100000, 0);
		int x = 0, y = 0, unitX = 0, unitY = 0;
		content->GetViewStart(&x, &y);
		content->GetScrollPixelsPerUnit(&unitX, &unitY);
		Check(x > 0, "Horizontal edge test must overflow the viewport");
		Check(x * unitX + content->GetClientSize().x == content->GetVirtualSize().x,
			"Horizontal scrolling exposes empty space beyond the content edge");
	}
}

void TestHeightCappedContent()
{
	wxDialog dialog(nullptr, wxID_ANY, "Tall dialog layout test");
	wxScrolledWindow *content = new wxScrolledWindow(&dialog);
	content->SetMinSize(wxSize(240, 200));
	ConfigureDialogScrolling(content);
	wxBoxSizer *fields = new wxBoxSizer(wxVERTICAL);
	wxPanel *section = new wxPanel(content);
	section->SetMinSize(wxSize(400, 2000));
	fields->Add(section, wxSizerFlags().Expand());
	content->SetSizer(fields);
	wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);
	top->Add(content, wxSizerFlags(1).Expand());
	dialog.SetSizer(top);
	FitScrollableDialog(&dialog, content);
	dialog.Layout();
	content->FitInside();
	Check(content->GetVirtualSize().y > content->GetClientSize().y,
		"Tall content should scroll vertically");
	Check(content->GetVirtualSize().x <= content->GetClientSize().x,
		"Vertical scrollbar unnecessarily forces horizontal scrolling");
}

void TestSmallContent()
{
	wxDialog dialog(nullptr, wxID_ANY, "Small dialog layout test");
	wxScrolledWindow *content = new wxScrolledWindow(&dialog);
	content->SetMinSize(dialog.FromDIP(wxSize(240, 200)));
	ConfigureDialogScrolling(content);
	wxBoxSizer *fields = new wxBoxSizer(wxVERTICAL);
	fields->Add(new wxStaticText(content, wxID_ANY, "A small setting"));
	content->SetSizer(fields);
	wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);
	top->Add(content, wxSizerFlags(1).Expand());
	top->Add(new wxButton(&dialog, wxID_OK), wxSizerFlags().Right());
	dialog.SetSizer(top);
	FitScrollableDialog(&dialog, content);
	dialog.Layout();
	Check(dialog.GetSize().x >= dialog.GetMinSize().x && dialog.GetSize().y >= dialog.GetMinSize().y,
		"Initial size ignores the fixed controls' minimum");
	Check(content->GetClientSize().x >= fields->GetMinSize().x &&
			content->GetClientSize().y >= fields->GetMinSize().y,
		"Small content should fit without scrolling");
}
} // namespace

int main(int argc, char **argv)
{
	// GUI tests need a display (for example xvfb-run on Linux CI). Do not report
	// success when the platform cannot initialize its GUI backend.
	if (!wxEntryStart(argc, argv)) {
		std::cerr << "No GUI display available; skipping dialog layout tests\n";
		return 77;
	}
	int result = 0;
	if (!wxTheApp->CallOnInit()) {
		result = 1;
	} else {
		try {
			TestScrollableContent();
			TestSmallContent();
			TestHeightCappedContent();
			TestHorizontalScrollEdge();
			std::cout << "Dialog layout regression tests passed\n";
		} catch (const std::exception &error) {
			std::cerr << error.what() << '\n';
			result = 1;
		}
		wxTheApp->OnExit();
	}
	wxEntryCleanup();
	return result;
}
