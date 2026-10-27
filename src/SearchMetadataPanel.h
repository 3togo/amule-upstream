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

#ifndef SEARCHMETADATAPANEL_H
#define SEARCHMETADATAPANEL_H

#include <wx/panel.h>
#include <array>

class wxTextCtrl;
class wxStaticText;
struct CSearchMetadata;

// The same controls and validation serve aMule and amulegui. Empty inputs impose
// no media constraint; hiding Extended Parameters leaves their text intact.
class CSearchMetadataPanel : public wxPanel
{
public:
	explicit CSearchMetadataPanel(wxWindow *parent);
	bool Read(CSearchMetadata &metadata, wxString &error, wxWindow *&invalidField) const;
	bool HasValues() const;
	void Reset();
	void SetSupported(bool supported);

private:
	enum Field
	{
		Artist,
		Album,
		Title,
		Codec,
		Length,
		Bitrate,
		FieldCount
	};
	std::array<wxTextCtrl *, FieldCount> m_fields;
	wxStaticText *m_help;
};

#endif // SEARCHMETADATAPANEL_H
