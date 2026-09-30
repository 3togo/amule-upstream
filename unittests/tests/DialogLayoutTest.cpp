// This file is part of the aMule Project.
// Copyright (c) 2026 aMule Team ( https://amule-org.github.io )
// Licensed under the GNU General Public License, version 2 or later.

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
	content->SetScrollRate(10, 10);

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

void TestSmallContent()
{
	wxDialog dialog(nullptr, wxID_ANY, "Small dialog layout test");
	wxScrolledWindow *content = new wxScrolledWindow(&dialog);
	content->SetMinSize(dialog.FromDIP(wxSize(240, 200)));
	content->SetScrollRate(10, 10);
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
