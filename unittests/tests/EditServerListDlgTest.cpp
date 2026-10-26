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

#include <wx/wx.h>
#include <wx/evtloop.h>

#include "EditServerListDlg.h"
#include "Preferences.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

// The preferences entry point shares the production translation unit with the
// dialog. Layout tests must never access or modify application preferences.
wxString CPreferences::s_configDir;
wxArrayString CPreferences::GetServerListURLs()
{
	throw std::runtime_error("Layout test unexpectedly read server sources");
}
bool CPreferences::SetServerListURLs(const wxArrayString &)
{
	throw std::runtime_error("Layout test unexpectedly saved server sources");
}

class EditServerListTestApp : public wxApp
{
public:
	bool OnInit() override { return true; }
};

wxIMPLEMENT_APP_NO_MAIN(EditServerListTestApp);

namespace
{
void Check(bool condition, const char *message)
{
	if (!condition) {
		throw std::runtime_error(message);
	}
}

template <typename T> T *FindControl(wxWindow &window)
{
	for (wxWindow *child : window.GetChildren()) {
		if (T *control = dynamic_cast<T *>(child)) {
			return control;
		}
	}
	throw std::runtime_error("Missing dialog control");
}

void Resize(EditServerListDlg &dialog, int width, int height = 300)
{
	const wxSize size = dialog.FromDIP(wxSize(width, height));
	dialog.SetClientSize(size);
	// Exercise the native size event path, without calling Layout() ourselves.
	dialog.SendSizeEvent();
	wxTheApp->Yield();
	Check(dialog.GetClientSize() == size, "Message prevented resizing the dialog");
}

void CheckLayout(EditServerListDlg &dialog)
{
	wxStaticText *label = FindControl<wxStaticText>(dialog);
	wxTextCtrl *editor = FindControl<wxTextCtrl>(dialog);
	wxClientDC dc(label);
	dc.SetFont(label->GetFont());
	const wxSize text = dc.GetMultiLineTextExtent(label->GetLabelText());
	// Native controls can use different line spacing from wxDC (notably Cocoa).
	// Recompute the label's required height instead of assuming wxDC's spacing.
	label->InvalidateBestSize();
	const wxSize required = label->GetBestSize();
	if (text.x > label->GetClientSize().x || required.y > label->GetClientSize().y) {
		std::cerr << "Message geometry: dialog=" << dialog.GetClientSize().x << 'x'
			  << dialog.GetClientSize().y << ", label=" << label->GetClientSize().x << 'x'
			  << label->GetClientSize().y << ", text=" << text.x << 'x' << text.y
			  << ", native best=" << required.x << 'x' << required.y
			  << ", font=" << label->GetFont().GetPointSize() << "pt\n"
			  << label->GetLabelText().ToUTF8().data() << '\n';
	}
	Check(text.x <= label->GetClientSize().x, "Message is clipped horizontally");
	Check(required.y <= label->GetClientSize().y, "Wrapped message is clipped vertically");
	Check(label->GetRect().GetBottom() < editor->GetRect().GetTop(),
		"Wrapped message overlaps the URL editor");
	const wxRect client(wxPoint(0, 0), dialog.GetClientSize());
	Check(client.Contains(label->GetRect()) && client.Contains(editor->GetRect()),
		"Message or editor extends beyond the dialog");
	for (int id : { wxID_OK, wxID_CANCEL }) {
		wxWindow *button = dialog.FindWindow(id);
		Check(button && client.Contains(button->GetRect()), "Dialog button is clipped");
		Check(editor->GetRect().GetBottom() < button->GetRect().GetTop(),
			"URL editor overlaps the dialog buttons");
	}
}

void TestResize(const wxString &message, bool largerFont)
{
	EditServerListDlg dialog(nullptr, "Server sources layout test", message, wxEmptyString);
	wxStaticText *label = FindControl<wxStaticText>(dialog);
	wxTextCtrl *editor = FindControl<wxTextCtrl>(dialog);
	if (largerFont) {
		wxFont font = label->GetFont();
		font.SetPointSize(font.GetPointSize() + 4);
		label->SetFont(font);
	}
	wxClientDC dc(label);
	dc.SetFont(label->GetFont());
	// The unwrapped translation must fit even with larger platform fonts.
	const int wideWidth = std::max(680, dialog.ToDIP(dc.GetMultiLineTextExtent(message).x) + 40);
	// Native widgets must be realized before measuring their layout. In particular,
	// wxGTK 3.2 applies a changed font to the native label when it is shown.
	dialog.SetClientSize(dialog.FromDIP(wxSize(wideWidth, 300)));
	dialog.Show();
	wxTheApp->Yield();
	const wxString urls = "https://example.org/server.met?one=1&two=2\nhttps://example.net/list";
	dialog.SetText(urls);
	editor->SetSelection(3, 12);

	Resize(dialog, 280);
	CheckLayout(dialog);
	const wxString narrowText = label->GetLabelText();
	const int narrowHeight = label->GetSize().y;
	Check(narrowText.Freq('\n') > message.Freq('\n'), "Narrow dialog did not wrap the message");

	Resize(dialog, wideWidth);
	CheckLayout(dialog);
	Check(label->GetLabelText() == message, "Widening did not restore the original text and line breaks");
	Check(label->GetSize().y < narrowHeight, "Widening left excess space below the message");
	const wxString wideText = label->GetLabelText();
	Resize(dialog, wideWidth, 380);
	CheckLayout(dialog);
	Check(label->GetLabelText() == wideText, "Height-only resize changed the wrapping");

	Resize(dialog, 280);
	CheckLayout(dialog);
	Check(label->GetLabelText() == narrowText && label->GetSize().y == narrowHeight,
		"Repeated resizing accumulated line breaks or changed the message height");
	Check(dialog.GetText() == urls, "Resizing changed the URL edits");
	long start = 0, end = 0;
	editor->GetSelection(&start, &end);
	Check(start == 3 && end == 12, "Resizing changed the editor selection");
}

void TestInitialLayout()
{
	const wxString message =
		"Enter one server list address on each line. "
		"These addresses are used to download server.met files.\nKeep each URL on its own line.";
	EditServerListDlg dialog(nullptr, "Initial layout test", message, wxEmptyString);
	dialog.Show();
	wxTheApp->Yield();
	CheckLayout(dialog);
	wxStaticText *label = FindControl<wxStaticText>(dialog);
	Check(label->GetLabelText().Freq('\n') > message.Freq('\n'),
		"Initial message was not wrapped when showing the dialog");
}

void TestLiteralText()
{
	const wxString message = "Addresses & sources && URLs\n\nOne URL per line.\n";
	EditServerListDlg dialog(nullptr, "Literal text test", message, wxEmptyString);
	dialog.Show();
	wxTheApp->Yield();
	Resize(dialog, 680);
	Check(FindControl<wxStaticText>(dialog)->GetLabelText() == message,
		"Literal ampersands or intentional blank lines were lost");
	Resize(dialog, 280);
	CheckLayout(dialog);
	Resize(dialog, 680);
	Check(FindControl<wxStaticText>(dialog)->GetLabelText() == message,
		"Resizing changed literal ampersands or intentional blank lines");
}
} // namespace

int main(int argc, char **argv)
{
	InstallGuiTestDiagnostics();
	if (!wxEntryStart(argc, argv)) {
		std::cerr << "No GUI display available; skipping server sources layout tests\n";
		return 77;
	}
	RestoreGuiTestLog();
	int result = 0;
	if (!wxTheApp->CallOnInit()) {
		result = 1;
	} else {
		try {
			wxEventLoop loop;
			wxEventLoopActivator active(&loop);
			const wxString english = "Enter one server.met URL per line.\nSaving replaces the "
						 "source list and removes comment lines.";
			const wxString russian = wxString::FromUTF8(
				"Введите по одному URL-адресу server.met на строку.\n"
				"Сохранение заменяет список источников и удаляет строки комментариев.");
			for (bool largerFont : { false, true }) {
				TestResize(english, largerFont);
				TestResize(russian, largerFont);
			}
			TestInitialLayout();
			TestLiteralText();
			std::cout << "Server sources layout regression tests passed\n";
		} catch (const std::exception &error) {
			std::cerr << error.what() << '\n';
			result = 1;
		}
		wxTheApp->OnExit();
	}
	wxEntryCleanup();
	return result;
}
