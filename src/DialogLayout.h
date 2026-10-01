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

#ifndef DIALOG_LAYOUT_H
#define DIALOG_LAYOUT_H

#include <wx/display.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>

// Horizontal scroll units must be exact pixels: larger units round the range
// past the virtual content edge and expose a blank strip at the right.
inline void ConfigureDialogScrolling(wxScrolledWindow *content)
{
	content->SetScrollRate(1, content->FromDIP(10));
}

// Keep the minimum dictated by the fixed controls, while bounding the initial
// window size to the work area of the parent's display.
inline void FitDialogToDisplay(wxWindow *dialog, const wxSize &preferredClientSize)
{
	dialog->GetSizer()->SetSizeHints(dialog);
	wxSize preferred = dialog->ClientToWindowSize(preferredClientSize);
	preferred.IncTo(dialog->GetMinSize());
	int displayIndex = wxDisplay::GetFromWindow(dialog->GetParent() ? dialog->GetParent() : dialog);
	if (displayIndex == wxNOT_FOUND) {
		displayIndex = 0;
	}
	const wxSize available = wxDisplay(displayIndex).GetClientArea().GetSize();
	preferred.DecTo(wxSize(available.GetWidth() * 4 / 5, available.GetHeight() * 4 / 5));
	dialog->SetSize(preferred);
}

// The scrollable content has a small explicit minimum; its natural size only
// influences the initial window size, never how far the user can shrink it.
inline void FitScrollableDialog(wxWindow *dialog, wxScrolledWindow *content)
{
	content->FitInside();
	const int fixedHeight =
		dialog->GetSizer()->GetMinSize().GetHeight() - content->GetMinSize().GetHeight();
	FitDialogToDisplay(dialog, content->GetSizer()->GetMinSize() + wxSize(0, fixedHeight));
}

#endif // DIALOG_LAYOUT_H
