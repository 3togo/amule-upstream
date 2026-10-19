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
#include <wx/evtloop.h>
#include <wx/fileconf.h>
#include <wx/mstream.h>
#include <iostream>
#include <stdexcept>
#include "MuleVirtualDataViewCtrl.h"
#include "Preferences.h"
#include <common/MenuIDs.h>

// These preferences are the only application state read by the production
// renderers/list base linked into this standalone GUI test.
uint8 CPreferences::s_depth3D = 0;
bool CPreferences::s_liveListSort = false;

class LayoutTestApp : public wxApp
{
public:
	bool OnInit() override { return true; }
};
wxIMPLEMENT_APP_NO_MAIN(LayoutTestApp);

namespace
{
void Check(bool condition, const char *message)
{
	if (!condition) {
		throw std::runtime_error(message);
	}
}

class TestList : public CMuleVirtualDataViewCtrl
{
public:
	explicit TestList(wxWindow *parent)
	: CMuleVirtualDataViewCtrl(parent)
	{
		AddTextColumn("Part", 0, "P", 50);
		AddTextColumn("Name", 1, "N", 200);
		AddTextColumn("Size", 2, "S", 80);
		SetColumnHidden(0, true, 0);
		AppendSpacerColumn(3);
		AssociateVirtualModel();
		m_columnStore.SetTableName("LayoutTest");
		LoadColumnSettings();
		InitColumnState();
	}
	using CMuleDataViewCtrl::CopyColumnLayoutFrom;
	using CMuleDataViewCtrl::RestoreColumnLayout;
	int CachedWidth(int col) const { return m_columnStore.GetCachedWidth(col); }
	int DefaultWidth(int col) const { return m_columnStore.GetColumnDefaultWidth(col); }
	int visibilityChanges = 0;
	int widthChanges = 0;

protected:
	wxString GetItemColumnText(wxUIntPtr, unsigned) const override { return "value"; }
	int CompareItemData(wxUIntPtr, wxUIntPtr, unsigned, bool, int) const override { return 0; }
	void OnColumnVisibilityChanged() override { ++visibilityChanges; }
	void OnColumnWidthsChanged() override
	{
		++widthChanges;
		CMuleDataViewCtrl::OnColumnWidthsChanged();
	}
};
class ConfigScope
{
public:
	ConfigScope()
	: input("", 0)
	, config(input)
	, previous(wxConfigBase::Set(&config))
	{
	}
	~ConfigScope() { wxConfigBase::Set(previous); }
	wxMemoryInputStream input;
	wxFileConfig config;
	wxConfigBase *previous;
};
void RunTests()
{
	ConfigScope fixture;
	{
		wxFrame frame(nullptr, wxID_ANY, "Column recovery test");
		auto *list = new TestList(&frame);
		auto *sizer = new wxBoxSizer(wxVERTICAL);
		sizer->Add(list, 1, wxEXPAND);
		frame.SetSizer(sizer);
		frame.SetSize(800, 500);
		frame.Show();
		wxTheApp->Yield(true);
		// GTK assigns native column widths when its first frame is painted.
		for (int attempt = 0; attempt < 100 && list->GetColumn(1)->GetWidth() == 0; ++attempt) {
			wxMilliSleep(10);
			wxTheApp->Yield(true);
		}
		Check(list->GetColumn(1)->GetWidth() > 0, "Native columns were not laid out");
		list->SetColumnHidden(2, true, 0);
		wxCommandEvent hide(wxEVT_MENU, MP_LISTCOL_1 + 1);
		list->ProcessWindowEvent(hide);
		Check(!list->IsColumnHidden(1), "Last visible column can be hidden");
		list->SetColumnHidden(2, false, 80);
		list->GetColumn(1)->SetWidth(333);
		wxTheApp->Yield(true);
		for (int attempt = 0; attempt < 100 && list->GetColumn(1)->GetWidth() != 333; ++attempt) {
			wxMilliSleep(10);
			wxTheApp->Yield(true);
		}
		const int adjustedWidth = list->GetColumn(1)->GetWidth();
		Check(adjustedWidth == 333, "Native resize did not take effect");
		const int visibilityChanges = list->visibilityChanges;
		const int widthChanges = list->widthChanges;
		list->RestoreColumnLayout(false);
		wxTheApp->Yield(true);
		Check(!list->IsColumnHidden(0) && !list->IsColumnHidden(2),
			"Show-all did not reveal every column");
		Check(list->GetColumn(1)->GetWidth() == adjustedWidth, "Show-all changed an existing width");
		list->RestoreColumnLayout(true);
		wxTheApp->Yield(true);
		Check(list->IsColumnHidden(0), "Reset lost the constructor's hidden Part column");
		Check(!list->IsColumnHidden(1) && !list->IsColumnHidden(2),
			"Reset failed to restore default visibility");
		for (int attempt = 0; attempt < 100 && list->GetColumn(1)->GetWidth() == adjustedWidth;
			++attempt) {
			wxMilliSleep(10);
			wxTheApp->Yield(true);
		}
		if (list->GetColumn(1)->GetWidth() == adjustedWidth) {
			std::cerr << "Adjusted and reset widths both " << adjustedWidth << '\n';
		}
		Check(list->GetColumn(1)->GetWidth() != adjustedWidth,
			"Reset failed to restore default widths");
		Check(list->visibilityChanges == visibilityChanges + 2 &&
				list->widthChanges == widthChanges + 2,
			"Reset did not notify search-tab synchronization hooks");
	}
	fixture.config.Write("/eMule/TableWidthsLayoutTest", "P:-444,N:300,S:80");
	{
		wxFrame first(nullptr, wxID_ANY, "Source layout");
		wxFrame second(nullptr, wxID_ANY, "Mirrored layout");
		auto *source = new TestList(&first);
		auto *mirror = new TestList(&second);
		Check(mirror->CachedWidth(0) == 444, "Hidden-width fixture did not load");
		mirror->CopyColumnLayoutFrom(*source);
		Check(mirror->CachedWidth(1) == 300,
			"Mirroring before first paint lost a restored visible width");
		source->RestoreColumnLayout(true);
		mirror->CopyColumnLayoutFrom(*source);
		Check(mirror->IsColumnHidden(0), "Mirroring lost hidden state");
		Check(mirror->CachedWidth(0) == source->DefaultWidth(0),
			"Mirroring retained a hidden column's pre-reset width");
		// Copying is passive: it must not emit notifications which echo back to
		// the source and continuously relayout two search tabs.
		Check(mirror->visibilityChanges == 0 && mirror->widthChanges == 0,
			"Copying a layout emitted change notifications");
	}

	fixture.config.Write("/eMule/TableWidthsLayoutTest", "P:-50,N:-200,S:-80");
	{
		wxFrame frame(nullptr, wxID_ANY, "Hidden profile recovery test");
		auto *list = new TestList(&frame);
		Check(list->IsColumnHidden(0) && !list->IsColumnHidden(1),
			"All-hidden profile did not recover the default identifying column");
	}
}

} // namespace
int main(int argc, char **argv)
{
	if (!wxEntryStart(argc, argv)) {
		std::cerr << "No GUI display available; skipping native list tests\n";
		return 77;
	}
	int result = 0;
	if (!wxTheApp->CallOnInit()) {
		result = 1;
	} else {
		try {
			wxEventLoop loop;
			wxEventLoopActivator active(&loop);
			RunTests();
			std::cout << "Native list regression tests passed\n";
		} catch (const std::exception &error) {
			std::cerr << error.what() << '\n';
			result = 1;
		}
		wxTheApp->OnExit();
	}
	wxEntryCleanup();
	return result;
}
