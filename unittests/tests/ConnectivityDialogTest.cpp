// This file is part of the aMule Project.
// Copyright (c) 2003-2026 aMule Team ( https://amule-org.github.io )
// Licensed under the GNU General Public License, version 2 or later.

#include "Connectivity.h"
#include <wx/wx.h>
#include <wx/dcscreen.h>
#include <wx/dcmemory.h>
#include <cstdlib>
#include <stdexcept>
#include <exception>
#include <iostream>

namespace
{
Connectivity::Snapshot s_snapshot;
int s_rechecks = 0;
} // namespace
Connectivity::Snapshot Connectivity::Capture()
{
	return s_snapshot;
}
void Connectivity::RecheckKad()
{
	++s_rechecks;
}
// Exercise the production dialog and timer with deterministic core evidence.
#include "ConnectivityDlg.cpp"

class ConnectivityTestApp : public wxApp
{
public:
	bool OnInit() override { return true; }
};
wxIMPLEMENT_APP_NO_MAIN(ConnectivityTestApp);

static void Check(bool value, const char *message)
{
	if (!value)
		throw std::runtime_error(message);
}

static wxButton *Button(wxWindow *parent, const wxString &label)
{
	for (auto *child : parent->GetChildren()) {
		auto *button = wxDynamicCast(child, wxButton);
		if (button && button->GetLabel() == label)
			return button;
	}
	throw std::runtime_error("Missing button");
}

static void Run()
{
	s_snapshot.tcpPort = 4662;
	s_snapshot.udpPort = 4672;
	s_snapshot.readAt = 1793350800;
	s_snapshot.tcpListening = true;
	s_snapshot.udpListening = true;
	s_snapshot.udpEnabled = true;
	s_snapshot.ed2k = Connectivity::Reachability::Reachable;
	s_snapshot.kadTCP = Connectivity::Reachability::Reachable;
	s_snapshot.kadUDP = Connectivity::Reachability::Blocked;
	auto *parent = new wxFrame(nullptr, wxID_ANY, "Connectivity test");
	parent->Show();
	Connectivity::ShowAssistant(parent);
	wxTheApp->Yield();
	auto *dialog = s_dialog.get();
	Check(dialog && !dialog->IsModal(), "Assistant must leave network controls accessible");
	Check(s_rechecks == 0, "Opening the assistant must not restart firewall checks");
	if (const char *screenshot = std::getenv("AMULE_CONNECTIVITY_SCREENSHOT")) {
		wxInitAllImageHandlers();
		wxScreenDC screen;
		wxBitmap bitmap(dialog->GetSize());
		wxMemoryDC memory(bitmap);
		memory.Blit(wxPoint(0, 0), dialog->GetSize(), &screen, dialog->GetPosition());
		memory.SelectObject(wxNullBitmap);
		Check(bitmap.SaveFile(wxString::FromUTF8(screenshot), wxBITMAP_TYPE_PNG),
			"Could not save screenshot");
	}
	auto *refresh = Button(dialog, "Refresh status");
	auto *recheck = Button(dialog, "Recheck Kad");
	Check(recheck->IsEnabled(), "Connected Kad cannot be rechecked");
	Connectivity::ShowAssistant(parent);
	Check(s_dialog.get() == dialog, "Repeated opening created another assistant");
	wxCommandEvent click(wxEVT_BUTTON, recheck->GetId());
	recheck->GetEventHandler()->ProcessEvent(click);
	Check(s_rechecks == 1, "Explicit recheck did not reach core");
	dialog->SetSize(dialog->FromDIP(wxSize(500, 350)));
	dialog->Layout();
	wxTheApp->Yield();
	for (auto *child : dialog->GetChildren()) {
		if (wxDynamicCast(child, wxButton)) {
			Check(child->GetRect().GetRight() < dialog->GetClientSize().GetWidth(),
				"Action button falls outside the narrow dialog");
		}
	}
	wxTextCtrl *report = nullptr;
	for (auto *child : dialog->GetChildren()) {
		if (auto *text = wxDynamicCast(child, wxTextCtrl))
			report = text;
	}
	Check(report, "Missing report control");
	report->SetSelection(0, 20);
	const wxString selected = report->GetStringSelection();
	const wxString unchangedReport = report->GetValue();
	++s_snapshot.readAt;
	dialog->SetResult(s_snapshot);
	Check(report->GetValue() == unchangedReport && report->GetStringSelection() == selected,
		"Timestamp polling changed the report buffer or selection");
	s_snapshot.kadUDP = Connectivity::Reachability::Reachable;
	dialog->SetResult(s_snapshot);
	Check(report->GetValue().Contains("Kad UDP: Reachable"),
		"Selected text prevented a new result from appearing");
	Check(!report->GetStringSelection().empty(), "Refreshing discarded the user's selection");
	s_snapshot.kadUDP = Connectivity::Reachability::Blocked;
	dialog->SetResult(s_snapshot);
	report->SetInsertionPointEnd();
	const long oldLength = report->GetLastPosition();
	s_snapshot.kadUDP = Connectivity::Reachability::Reachable;
	dialog->SetResult(s_snapshot);
	Check(report->GetLastPosition() < oldLength, "Fixture did not shorten the report");
	Check(report->GetInsertionPoint() <= report->GetLastPosition(),
		"Shorter report retained an invalid caret position");
	s_snapshot.kadUDP = Connectivity::Reachability::Checking;
	dialog->SetResult(s_snapshot);
	Check(!recheck->IsEnabled(), "Pending Kad check can be restarted indefinitely");
	Check(recheck->IsShown(), "Disabled action disappeared");
	s_snapshot.kadTCP = Connectivity::Reachability::Disconnected;
	s_snapshot.kadUDP = Connectivity::Reachability::Disconnected;
	dialog->SetResult(s_snapshot);
	Check(!recheck->IsEnabled(), "Disconnected Kad offers a check it cannot run");
	dialog->SetError("Core disconnected");
	Check(refresh->IsEnabled(), "Error prevents manual refresh");
	Check(!recheck->IsEnabled(), "Error leaves stale recheck enabled");
	// Preferences is modal. A first-run view owned by the main frame must not
	// leave the settings entry raising a disabled sibling of that modal.
	auto *settings = new wxDialog(parent, wxID_ANY, "Settings");
	std::exception_ptr modalError;
	wxTheApp->CallAfter([&]() {
		try {
			Connectivity::ShowAssistant(settings);
			Check(s_dialog && s_dialog->GetParent() == settings,
				"Settings reused an assistant owned by a different window");
			Check(s_dialog->IsEnabled(), "Assistant is disabled by modal settings");
		} catch (...) {
			modalError = std::current_exception();
		}
		settings->EndModal(wxID_OK);
	});
	settings->ShowModal();
	if (modalError)
		std::rethrow_exception(modalError);
	dialog = s_dialog.get();
	auto *closing = dialog;
	closing->Destroy();
	Connectivity::ShowAssistant(settings);
	Check(s_dialog && s_dialog.get() != closing,
		"Reopening before deferred deletion reused a closing assistant");
	dialog = s_dialog.get();
	dialog->Destroy();
	wxTheApp->ProcessPendingEvents();
	wxTheApp->ProcessIdle();
	Check(!s_dialog, "Destroyed assistant retained a dangling pointer");
	settings->Destroy();
	parent->Destroy();
	wxTheApp->ProcessPendingEvents();
	wxTheApp->ProcessIdle();
}

int main(int argc, char **argv)
{
	if (!wxEntryStart(argc, argv))
		return 77;
	if (!wxTheApp->CallOnInit())
		return 1;
	int result = 0;
	try {
		Run();
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		result = 1;
	}
	wxTheApp->OnExit();
	wxEntryCleanup();
	return result;
}
