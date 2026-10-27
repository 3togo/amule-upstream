//
// This file is part of the aMule Project.
//
// Copyright (c) 2003-2026 aMule Team ( https://amule-org.github.io )
// Copyright (c) 2002-2011 Merkur ( devs@emule-project.net / http://www.emule-project.net )
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

#include "SearchMetadataPanel.h"

#include <ec/cpp/SearchMetadata.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <algorithm>

CSearchMetadataPanel::CSearchMetadataPanel(wxWindow *parent)
: wxPanel(parent)
{
	const std::array<wxString, FieldCount> labels = { _("Artist"),
		_("Album"),
		_("Title"),
		_("Codec"),
		_("Minimum duration"),
		_("Minimum bitrate (kbps)") };
	auto *grid = new wxFlexGridSizer(4, FromDIP(6), FromDIP(12));
	grid->AddGrowableCol(1);
	grid->AddGrowableCol(3);
	for (size_t i = 0; i < m_fields.size(); ++i) {
		grid->Add(new wxStaticText(this, wxID_ANY, labels[i]), wxSizerFlags().CenterVertical());
		m_fields[i] = new wxTextCtrl(this,
			wxID_ANY,
			wxEmptyString,
			wxDefaultPosition,
			FromDIP(wxSize(130, -1)),
			wxTE_PROCESS_ENTER);
		m_fields[i]->SetName(labels[i]);
		grid->Add(m_fields[i], wxSizerFlags(1).Expand());
	}
	m_fields[Length]->SetHint(_("seconds or h:mm:ss"));
	m_fields[Length]->SetToolTip(
		_("Minimum playing time, up to 24:00:00. Empty or 0 means any duration."));
	m_fields[Bitrate]->SetToolTip(_("Minimum bitrate in kbps, up to 1000000. Empty or 0 means any "
					"bitrate. Effective on Kad; eD2k servers may ignore it."));
	for (Field field : { Artist, Album, Title, Codec }) {
		m_fields[field]->SetToolTip(
			_("Media metadata, separate from the filename query. Empty means any value."));
	}
	m_help = new wxStaticText(this, wxID_ANY, wxEmptyString);
	m_help->SetToolTip(_("Kad cannot run two searches for the same keyword. Stop the existing search "
			     "before changing its filters."));
	auto *layout = new wxBoxSizer(wxVERTICAL);
	layout->Add(grid, wxSizerFlags().Expand());
	layout->Add(m_help, wxSizerFlags().Expand().Border(wxTOP, FromDIP(6)));
	SetSizer(layout);
	SetSupported(true);
}

bool CSearchMetadataPanel::Read(CSearchMetadata &metadata, wxString &error, wxWindow *&invalidField) const
{
	CSearchMetadata value;
	value.artist = m_fields[Artist]->GetValue();
	value.album = m_fields[Album]->GetValue();
	value.title = m_fields[Title]->GetValue();
	value.codec = m_fields[Codec]->GetValue();
	const wxString bitrate = wxString(m_fields[Bitrate]->GetValue()).Trim(true).Trim(false);
	wxULongLong_t parsed = 0;
	if (!bitrate.empty() &&
		(!std::all_of(
			 bitrate.begin(), bitrate.end(), [](wxUniChar c) { return c >= '0' && c <= '9'; }) ||
			!bitrate.ToULongLong(&parsed) || parsed > CSearchMetadata::MaxBitrateKbps)) {
		error = _("Minimum bitrate must be a whole number between 0 and 1000000 kbps.");
		invalidField = m_fields[Bitrate];
		return false;
	}
	value.minBitrateKbps = parsed;
	const wxString duration = wxString(m_fields[Length]->GetValue()).Trim(true).Trim(false);
	if (!duration.empty() &&
		!CSearchMetadata::ParseLength(std::string(duration.utf8_str()), value.minLength)) {
		error = _("Minimum duration must be seconds or h:mm:ss, up to 24:00:00.");
		invalidField = m_fields[Length];
		return false;
	}
	std::string validationError;
	if (!value.Validate(validationError)) {
		error = _("Media filters must not contain NUL characters.");
		invalidField = nullptr;
		return false;
	}
	metadata = value;
	error.clear();
	invalidField = nullptr;
	return true;
}

bool CSearchMetadataPanel::HasValues() const
{
	return std::any_of(m_fields.begin(), m_fields.end(), [](const wxTextCtrl *field) {
		return !field->GetValue().empty();
	});
}

void CSearchMetadataPanel::Reset()
{
	for (wxTextCtrl *field : m_fields)
		field->Clear();
}

void CSearchMetadataPanel::SetSupported(bool supported)
{
	for (wxTextCtrl *field : m_fields)
		field->Enable(supported);
	m_help->SetLabel(
		supported ? _("Minimum bitrate is effective on Kad; eD2k servers may ignore media filters.")
			  : _("The connected daemon does not support media filters. Update the daemon to use "
			      "these fields."));
	m_help->Wrap(FromDIP(650));
	Layout();
}
