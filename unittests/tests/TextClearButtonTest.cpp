// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include <wx/wx.h>
#include <wx/evtloop.h>

#include "TextClearButton.h"

#include <iostream>
#include <stdexcept>
#include <vector>

class ClearButtonTestApp : public wxApp
{
public:
	bool OnInit() override { return true; }
};
wxIMPLEMENT_APP_NO_MAIN(ClearButtonTestApp);

namespace
{
void Require(bool condition, const char *message)
{
	if (!condition) {
		throw std::runtime_error(message);
	}
}

// On wxMSW FromDIP() scales coordinates, unlike wxGTK/wxOSX. Override
// both DPI queries so this exercises the same bundle request at 100% and 200%.
class DpiTextCtrl : public wxTextCtrl
{
public:
	DpiTextCtrl(wxWindow *parent, double scale)
	: wxTextCtrl(parent, wxID_ANY)
	, m_scale(scale)
	{
	}
	wxSize GetDPI() const override { return wxSize(96 * m_scale, 96 * m_scale); }
	double GetDPIScaleFactor() const override { return m_scale; }

private:
	double m_scale;
};

class RecordingArtProvider : public wxArtProvider
{
public:
	std::vector<wxSize> requestedSizes;

protected:
	wxBitmapBundle CreateBitmapBundle(
		const wxArtID &id, const wxArtClient &client, const wxSize &size) override
	{
		if (id == wxART_CROSS_MARK && client == wxART_BUTTON) {
			requestedSizes.push_back(size);
		}
		return wxBitmapBundle(); // Keep the real native artwork.
	}
};

class TextCounter : public wxEvtHandler
{
public:
	int count = 0;
	void OnText(wxCommandEvent &event)
	{
		++count;
		event.Skip();
	}
};

void CheckRow(wxWindow *field, wxSizer *outer)
{
	auto *parent = field->GetParent();
	const auto id = field->GetId();
	auto *entry = dynamic_cast<wxTextEntry *>(field);
	auto *row = WithTextClearButton(field);
	outer->Add(row, wxSizerFlags().Expand());
	outer->Layout();
	auto *clear = dynamic_cast<wxBitmapButton *>(row->GetItem(size_t(1))->GetWindow());
	Require(clear && clear->IsShown(), "Clear button must always be visible");
	Require(clear->IsEnabled() == !entry->GetValue().IsEmpty(), "Initial enabled state");
	const wxSize buttonMin = clear->GetMinSize();
	Require(buttonMin.x == buttonMin.y && buttonMin.y >= field->GetBestSize().y,
		"Square button must accommodate the text field");

	const wxRect geometry = field->GetRect();
	entry->SetValue("ubuntu");
	Require(clear->IsShown() && clear->IsEnabled(), "Text enables clear");
	Require(field->GetRect() == geometry, "Typing must not move the field");

	TextCounter counter;
	// Skip so the helper and existing application handlers receive the event.
	parent->Bind(wxEVT_TEXT, &TextCounter::OnText, &counter, id);
	wxCommandEvent click(wxEVT_BUTTON, clear->GetId());
	clear->GetEventHandler()->ProcessEvent(click);
	Require(entry->GetValue().IsEmpty(), "Button clears the field");
	Require(counter.count == 1, "Clear must propagate exactly one text event");
	Require(clear->IsShown() && !clear->IsEnabled(), "Empty clear stays visible and disabled");
	Require(field->GetRect() == geometry, "Clearing must not move the field");
	if (auto *combo = dynamic_cast<wxComboBox *>(field)) {
		Require(combo->GetCount() == 1 && combo->GetString(0) == "saved term",
			"Clearing text must preserve saved history");
	}

	// Search replaces its Name field in the same sizer when history is toggled.
	for (bool useCombo : { false, true }) {
		wxWindow *replacement = useCombo ? static_cast<wxWindow *>(new wxComboBox(parent, id))
						 : static_cast<wxWindow *>(new wxTextCtrl(parent, id));
		row->Replace(field, replacement);
		field->Destroy();
		field = replacement;
		entry = dynamic_cast<wxTextEntry *>(field);
		entry->SetValue("replacement");
		Require(clear->IsEnabled(), "Replacement text enables clear");
		clear->GetEventHandler()->ProcessEvent(click);
		Require(entry->GetValue().IsEmpty() && !clear->IsEnabled(), "Clear follows replacement ID");
		Require(clear->IsShown(), "Replacement empty state stays visible");
	}
	parent->Unbind(wxEVT_TEXT, &TextCounter::OnText, &counter, id);
}
} // namespace

int main(int argc, char **argv)
{
	delete wxLog::SetActiveTarget(new wxLogStderr);
	if (!wxEntryStart(argc, argv)) {
		std::cerr << "No GUI display: clear-button checks skipped\n";
		return 77;
	}
	if (!wxTheApp->CallOnInit()) {
		wxEntryCleanup();
		return 1;
	}
	int status = 0;
	{
		wxGUIEventLoop loop;
		wxEventLoopActivator active(&loop);
		auto *frame = new wxFrame(nullptr, wxID_ANY, "Clear button regression checks");
		auto *outer = new wxBoxSizer(wxVERTICAL);
		frame->SetSizer(outer);
		frame->SetClientSize(600, 400);
		frame->Show();
		wxYield();
		try {
			for (double scale : { 1.0, 2.0 }) {
				auto *provider = new RecordingArtProvider;
				wxArtProvider::Push(provider); // Also clears the art-provider cache.
				try {
					CheckRow(new DpiTextCtrl(frame, scale), outer);
					// Native fallback artwork may re-enter the provider with pixel
					// sizes while rasterizing (e.g. 40x40 on Retina). Exercise a
					// later high-resolution request on every platform and check
					// the helper's initial bundle request, not the last raster size.
					const auto highResolution = wxArtProvider::GetBitmapBundle(
						wxART_CROSS_MARK, wxART_BUTTON, wxSize(40, 40));
					Require(highResolution.IsOk(),
						"High-resolution artwork must be available");
					Require(!provider->requestedSizes.empty() &&
							provider->requestedSizes.front() == wxSize(20, 20),
						"Initial bitmap bundle size must remain 20 DIP at every DPI");
				} catch (...) {
					wxArtProvider::Pop();
					throw;
				}
				wxArtProvider::Pop();
			}
			auto *combo = new wxComboBox(frame, wxID_ANY);
			combo->Append("saved term");
			combo->SetValue("initial text");
			CheckRow(combo, outer);
		} catch (const std::exception &error) {
			std::cerr << error.what() << '\n';
			status = 1;
		}
		frame->Destroy();
		wxYield();
	}
	wxTheApp->OnExit();
	wxEntryCleanup();
	return status;
}
