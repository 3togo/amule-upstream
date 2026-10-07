//								-*- C++ -*-
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

#ifndef AMULE_KADCONTACTHISTOGRAM_H
#define AMULE_KADCONTACTHISTOGRAM_H
#include <wx/panel.h>
#include <wx/dcbuffer.h>
#include <wx/intl.h>
#include <wx/textwrapper.h>
#include <vector>
#include <algorithm>
#include "kademlia/utils/ContactDistribution.h"
// Snapshot view; drawing never traverses live contacts or changes routing.
class CKadContactHistogram final : public wxPanel
{
public:
	explicit CKadContactHistogram(wxWindow *parent)
	: wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, 150))
	{
		SetMinSize(wxSize(200, 150));
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		SetName("kadContactDistribution");
		SetToolTip(_("Contacts grouped by the first six bits of their KadID. Blue: all contacts; "
			     "green: verified addresses. Clustering near your own KadID is expected."));
		Bind(wxEVT_PAINT, &CKadContactHistogram::Paint, this);
	}
	void SetDistribution(const Kademlia::ContactDistribution &data, bool available)
	{
		m_data = data;
		m_available = available;
		Refresh(false);
	}

private:
	class SummaryWrapper : public wxTextWrapper
	{
	public:
		std::vector<wxString> lines;

	protected:
		void OnOutputLine(const wxString &line) override { lines.push_back(line); }
	};
	void Paint(wxPaintEvent &)
	{
		wxAutoBufferedPaintDC dc(this);
		dc.SetBackground(wxBrush(GetBackgroundColour()));
		dc.Clear();
		dc.SetTextForeground(GetForegroundColour());
		dc.SetFont(GetFont());
		if (!m_available) {
			SummaryWrapper wrapped;
			wrapped.Wrap(this,
				_("Contact distribution is unavailable from this core."),
				std::max(1, GetClientSize().x - 16));
			int y = 8;
			for (const auto &line : wrapped.lines) {
				dc.DrawText(line, 8, y);
				y += dc.GetCharHeight();
			}
			return;
		}
		const auto size = GetClientSize();
		// Summary wraps at narrow widths and with longer translations.
		wxString summary = wxString::Format(
			_("KadID distribution: %u contacts, %u verified, %u distinct /24 subnets"),
			m_data.Total(),
			m_data.Verified(),
			m_data.subnets);
		SummaryWrapper wrapped;
		wrapped.Wrap(this, summary, std::max(1, size.x - 16));
		int y = 5;
		for (const auto &line : wrapped.lines) {
			dc.DrawText(line, 8, y);
			y += dc.GetCharHeight();
		}
		const int top = y + 7;
		const int left = 36, bottom = size.y - dc.GetCharHeight() - 6;
		const int width = size.x - left - 8, height = bottom - top;
		if (width <= 0 || height <= 0) {
			return;
		}
		const uint32_t peak =
			std::max(1u, *std::max_element(m_data.contacts.begin(), m_data.contacts.end()));
		dc.SetPen(wxPen(GetForegroundColour()));
		dc.DrawLine(left, top, left, bottom);
		dc.DrawLine(left, bottom, left + width, bottom);
		dc.DrawText(wxString::Format("%u", peak), 2, top);
		for (size_t i = 0; i < m_data.BinCount; ++i) {
			const int x = left + static_cast<int>(i * width / m_data.BinCount);
			const int next = left + static_cast<int>((i + 1) * width / m_data.BinCount);
			const int barWidth = std::max(1, next - x - 1);
			auto bar = [&](uint32_t count, const wxColour &colour) {
				const int h = static_cast<int>(static_cast<uint64_t>(count) * height / peak);
				if (h) {
					dc.SetPen(*wxTRANSPARENT_PEN);
					dc.SetBrush(wxBrush(colour));
					dc.DrawRectangle(x, bottom - h, barWidth, h);
				}
			};
			bar(m_data.contacts[i], wxColour(70, 130, 210));
			bar(m_data.verified[i], wxColour(35, 160, 90));
		}
		dc.DrawText("00", left, bottom + 2);
		dc.DrawText("ff", left + width - dc.GetTextExtent("ff").x, bottom + 2);
	}
	Kademlia::ContactDistribution m_data;
	bool m_available = false;
};
#endif
