//
// This file is part of the aMule Project.
//
// Copyright (c) 2003-2026 aMule Team ( https://amule-org.github.io )
// Copyright (c) 2002-2011 Drager
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

#include <wx/dialog.h> // Needed for wxDialog	// Do_not_auto_remove
#include <wx/sizer.h>  // Needed for wxBoxSizer
#include <wx/file.h>   // Needed for wxFile
#include <wx/log.h>    // Needed for wxLogSysError
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/tokenzr.h>
#include <wx/msgdlg.h>
#include "Preferences.h"
#ifdef CLIENT_GUI
#include "amule.h"
#include "amule-remote-gui.h"
#endif

#include "EditServerListDlg.h" // Interface declarations

wxBEGIN_EVENT_TABLE(EditServerListDlg, wxDialog)
	EVT_BUTTON(wxID_OK, EditServerListDlg::OnOK)
wxEND_EVENT_TABLE()

EditServerListDlg::EditServerListDlg(
	wxWindow *parent, const wxString &caption, const wxString &message, const wxString &filename)
: wxDialog(parent, -1, caption, wxDefaultPosition, wxSize(400, 200), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
, m_messageText(message)
{
	m_file = filename;

	wxBeginBusyCursor();

	wxBoxSizer *topsizer = new wxBoxSizer(wxVERTICAL);

	m_message = new wxStaticText(
		this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE);
	topsizer->Add(m_message, wxSizerFlags().Expand().Border(wxALL, 10));

	m_textctrl = new wxTextCtrl(this, -1, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE);
	topsizer->Add(m_textctrl, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT, 15));

	topsizer->Add(CreateButtonSizer(wxOK | wxCANCEL), wxSizerFlags().Center().Border(wxALL, 10));

	SetAutoLayout(TRUE);
	SetSizer(topsizer);
	Layout();

	Centre(wxBOTH);

	if (!filename.empty() && wxFile::Exists(filename))
		m_textctrl->LoadFile(filename);

	m_textctrl->SetFocus();

	wxEndBusyCursor();
}

EditServerListDlg::~EditServerListDlg() = default;

bool EditServerListDlg::Layout()
{
	if (GetSizer()) {
		const int width =
			GetClientSize().GetWidth() - 2 * GetSizer()->GetItem(m_message)->GetBorder();
		if (width > 0) {
			// Wrap() only adds line breaks. Restore the original message so it can
			// also unwrap when the dialog grows, preserving intentional newlines.
			m_message->SetLabelText(m_messageText);
			m_message->Wrap(width);
			// wxST_NO_AUTORESIZE leaves the cached best size unchanged.
			m_message->InvalidateBestSize();
		}
	}
	return wxDialog::Layout();
}

void EditServerListDlg::OnOK(wxCommandEvent &WXUNUSED(event))
{
	if (m_file.empty() || m_textctrl->SaveFile(m_file))
		EndModal(1);
	else
		wxLogSysError("Can't write to file '" + m_file + "'");
}
wxString EditServerListDlg::GetText() const
{
	return m_textctrl->GetValue();
}
void EditServerListDlg::SetText(const wxString &text)
{
	m_textctrl->SetValue(text);
}

void EditServerListSources(wxWindow *parent)
{
	EditServerListDlg dlg(parent,
		_("Edit server list sources"),
		_("Enter one server.met URL per line.\nSaving replaces the source list and removes comment "
		  "lines."),
		wxEmptyString);
	wxString text;
	for (const auto &url : thePrefs::GetServerListURLs())
		text += url + "\n";
	dlg.SetText(text);
	while (dlg.ShowModal() == 1) {
		wxArrayString urls;
		wxStringTokenizer lines(dlg.GetText(), "\r\n");
		while (lines.HasMoreTokens()) {
			wxString url = lines.GetNextToken();
			url.Trim().Trim(false);
			if (!url.empty() && !url.StartsWith("#"))
				urls.Add(url);
		}
		if (thePrefs::SetServerListURLs(urls)) {
#ifdef CLIENT_GUI
			theApp->glob_prefs->SendChangesToRemote();
#endif
			return;
		}
		wxMessageBox(wxString::Format(_("Could not save server list sources to %s. Check the URLs "
						"and file permissions."),
#ifdef CLIENT_GUI
				     _("addresses.dat in the daemon configuration directory")
#else
				     thePrefs::GetConfigDir() + "addresses.dat"
#endif
					     ),
			_("ERROR"),
			wxOK | wxICON_ERROR,
			parent);
	}
}
// File_checked_for_headers
