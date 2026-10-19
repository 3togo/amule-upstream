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
		AddTextColumn("Name", 0, "N", 100);
		AddBarColumn("Composite name", 1, "C", 80);
		AddBarColumn("Progress", 2, "P", 120);
		AppendSpacerColumn(3);
		AssociateVirtualModel();
		ApplySorting(0, 0);
		InitColumnState();
		SetSize(1000, 400);
	}
	using CMuleDataViewCtrl::FitColumnWidths;
	using CMuleVirtualDataViewCtrl::GetColumnFitRows;
	mutable unsigned measurements = 0;

protected:
	void GetDisplayOrder(wxDataViewItemArray &) const override
	{
		throw std::runtime_error("Fitter materialized the full display order");
	}
	wxString GetItemColumnText(wxUIntPtr data, unsigned) const override
	{
		return data == 4096 ? wxString('W', 25) : wxString("short");
	}
	int GetCustomFitWidth(const wxDataViewItem &, unsigned column, wxDC &) const override
	{
		++measurements;
		return column == 1 ? 350 : -1;
	}
	int CompareItemData(wxUIntPtr a, wxUIntPtr b, unsigned, bool, int) const override
	{
		return (a > b) - (a < b);
	}
};
void RunTests()
{
	wxFrame frame(nullptr, wxID_ANY, "Column fitting test");
	auto *list = new TestList(&frame);
	for (wxUIntPtr i = 1; i <= 4096; ++i) {
		list->AppendItemData(i);
	}
	list->FinishBulkLoad();
	auto *sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(list, 1, wxEXPAND);
	frame.SetSizer(sizer);
	frame.SetSize(1000, 500);
	frame.Show();
	wxTheApp->Yield(true);
	wxDataViewItemArray sample;
	list->GetColumnFitRows(sample, 512);
	Check(sample.size() == 512, "Fit sample is not bounded");
	Check(list->ItemAt(list->GetModelRow(sample.front())) == 1 &&
			list->ItemAt(list->GetModelRow(sample.back())) == 4096,
		"Fit sample omitted either end of the list");
	list->FitColumnWidths(-1);
	wxTheApp->Yield(true);
	for (int attempt = 0; attempt < 100 && list->GetColumn(1)->GetWidth() < 350; ++attempt) {
		wxMilliSleep(10);
		wxTheApp->Yield(true);
	}
	Check(list->GetColumn(0)->GetWidth() > 100, "Long sampled text was not fitted");
	Check(list->GetColumn(1)->GetWidth() >= 350,
		"Composite cell used a fixed bar width instead of measured content");
	const int barWidth = list->GetColumn(2)->GetWidth();
	const int nameWidth = list->GetColumn(1)->GetWidth();
	list->FitColumnWidths(-1);
	wxTheApp->Yield(true);
	Check(list->GetColumn(2)->GetWidth() == barWidth && list->GetColumn(1)->GetWidth() == nameWidth,
		"Repeated fit grows widths using their previous values");
	list->SetColumnHidden(1, true, 0);
	list->FitColumnWidths(-1);
	wxTheApp->Yield(true);
	Check(list->IsColumnHidden(1), "Fitting revealed a hidden column");
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
