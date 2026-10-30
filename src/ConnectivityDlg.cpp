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

#include "Connectivity.h"

#include <wx/wx.h>
#include <wx/timer.h>
#include <wx/weakref.h>
#include <wx/wrapsizer.h>
#include <algorithm>
#ifdef CLIENT_GUI
#include "amule.h"
#include "libs/ec/cpp/RemoteConnect.h"
#endif

namespace
{
class ConnectivityDialog;
#ifdef CLIENT_GUI
class ConnectivityReply final : public CECPacketHandlerBase
{
	wxWeakRef<ConnectivityDialog> m_dialog;

public:
	explicit ConnectivityReply(ConnectivityDialog *dialog);
	void HandlePacket(const CECPacket *packet) override;
	void AbortPendingRequest() override;
};
#endif

class ConnectivityDialog final : public wxDialog
{
	wxTextCtrl *m_text;
	wxStaticText *m_status;
	wxButton *m_refresh;
	wxButton *m_recheck;
	wxTimer m_timer;
	bool m_pending = false;
	bool m_closing = false;

public:
	explicit ConnectivityDialog(wxWindow *parent)
	: wxDialog(parent,
		  wxID_ANY,
		  _("Connectivity assistant"),
		  wxDefaultPosition,
		  parent->FromDIP(wxSize(780, 600)),
		  wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	, m_timer(this)
	{
		auto *sizer = new wxBoxSizer(wxVERTICAL);
		auto *intro = new wxStaticText(this,
			wxID_ANY,
			_("Check the core's ports, router mapping, and eD2k / Kad reachability."));
		sizer->Add(intro, wxSizerFlags().Border(wxALL, FromDIP(10)));
		m_status = new wxStaticText(this, wxID_ANY, wxEmptyString);
		sizer->Add(m_status, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10)));
		m_text = new wxTextCtrl(this,
			wxID_ANY,
			wxEmptyString,
			wxDefaultPosition,
			wxDefaultSize,
			wxTE_MULTILINE | wxTE_READONLY | wxTE_WORDWRAP);
		sizer->Add(m_text, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT, FromDIP(10)));
		auto *buttons = new wxWrapSizer(wxHORIZONTAL);
		m_refresh = new wxButton(this, wxID_ANY, _("Refresh status"));
		m_recheck = new wxButton(this, wxID_ANY, _("Recheck Kad"));
		m_recheck->Disable();
		auto *guide = new wxButton(this, wxID_ANY, _("Connectivity guide"));
		auto *close = new wxButton(this, wxID_CLOSE, _("Close"));
		for (auto *button : { m_refresh, m_recheck, guide, close }) {
			buttons->Add(button, wxSizerFlags().Border(wxRIGHT, FromDIP(6)));
		}
		sizer->Add(buttons, wxSizerFlags().Expand().Border(wxALL, FromDIP(10)));
		SetSizer(sizer);
		SetMinSize(FromDIP(wxSize(500, 350)));
		m_refresh->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { RefreshStatus(false); });
		m_recheck->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { RefreshStatus(true); });
		guide->Bind(wxEVT_BUTTON, [](wxCommandEvent &) {
			wxLaunchDefaultBrowser(
				"https://amule-org.github.io/docs/manual/configuration/network-connectivity");
		});
		close->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { Destroy(); });
		Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &) {
			m_timer.Stop();
			Destroy();
		});
		Bind(wxEVT_TIMER, [this](wxTimerEvent &) { RefreshStatus(false); }, m_timer.GetId());
		CentreOnParent();
		m_timer.Start(2000);
		RefreshStatus(false);
	}
	bool IsClosing() const { return m_closing; }
	bool Destroy() override
	{
		if (m_closing)
			return true;
		m_closing = true;
		m_timer.Stop();
		return wxDialog::Destroy();
	}
	void SetResult(const Connectivity::Snapshot &snapshot)
	{
		if (m_closing)
			return;
		m_pending = false;
		const wxString report = Connectivity::Format(snapshot);
		m_status->SetLabel(report.BeforeFirst('\n'));
		const wxString body = report.AfterFirst('\n');
		// Poll timestamps update independently. Leave the native text buffer alone
		// while its content is unchanged, preserving scroll position and selections.
		if (m_text->GetValue() != body) {
			long start = 0, end = 0;
			m_text->GetSelection(&start, &end);
			m_text->ChangeValue(body);
			const long last = m_text->GetLastPosition();
			m_text->SetSelection(std::min(start, last), std::min(end, last));
		}
		m_refresh->Enable();
		m_recheck->Enable(Connectivity::CanRecheckKad(snapshot));
	}
	void SetError(const wxString &message)
	{
		if (m_closing)
			return;
		m_pending = false;
		m_status->SetLabel(wxEmptyString);
		m_text->ChangeValue(message);
		m_recheck->Disable();
		m_refresh->Enable();
		m_timer.Stop();
	}
	void RefreshStatus(bool recheck)
	{
		if (m_pending || m_closing)
			return;
#ifdef CLIENT_GUI
		if (!theApp->m_connect->ServerSupportsConnectivity()) {
			SetError(_("The connected core does not support the connectivity assistant. Upgrade "
				   "the core to use this feature."));
			return;
		}
		if (!m_timer.IsRunning())
			m_timer.Start(2000);
		m_pending = true;
		m_refresh->Disable();
		m_recheck->Disable();
		CECPacket request(EC_OP_GET_CONNECTIVITY);
		if (recheck)
			request.AddTag(CECEmptyTag(EC_TAG_CONNECTIVITY_RECHECK));
		theApp->m_connect->SendRequest(new ConnectivityReply(this), &request);
#else
		if (recheck)
			Connectivity::RecheckKad();
		SetResult(Connectivity::Capture());
#endif
	}
};

#ifdef CLIENT_GUI
ConnectivityReply::ConnectivityReply(ConnectivityDialog *dialog)
: m_dialog(dialog)
{
}
void ConnectivityReply::HandlePacket(const CECPacket *packet)
{
	if (m_dialog) {
		Connectivity::Snapshot snapshot;
		if (Connectivity::Decode(*packet, snapshot))
			m_dialog->SetResult(snapshot);
		else
			m_dialog->SetError(_("The core returned an invalid connectivity report."));
	}
	delete this;
}
void ConnectivityReply::AbortPendingRequest()
{
	if (m_dialog)
		m_dialog->SetError(_("Connection to the core was lost. Reconnect and refresh the report."));
	delete this;
}
#endif
wxWeakRef<ConnectivityDialog> s_dialog;
} // namespace

void Connectivity::ShowAssistant(wxWindow *parent)
{
	// A view owned by the main window is disabled while Preferences is modal.
	// Reopen under the caller so the settings entry is usable even when the
	// first-run view was left open. Outstanding replies only hold weak refs.
	if (s_dialog &&
		(s_dialog->IsClosing() || s_dialog->IsBeingDeleted() || s_dialog->GetParent() != parent)) {
		s_dialog->Destroy();
		s_dialog = nullptr;
	}
	if (!s_dialog)
		s_dialog = new ConnectivityDialog(parent);
	s_dialog->Show();
	s_dialog->Raise();
}
