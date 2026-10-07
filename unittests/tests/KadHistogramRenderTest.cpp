// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later
#include <wx/wx.h>
#include "KadContactHistogram.h"
#include <iostream>
#include <stdexcept>

class HistogramTestApp : public wxApp
{
public:
	bool OnInit() override { return true; }
};
wxIMPLEMENT_APP_NO_MAIN(HistogramTestApp);

struct KadHistogramTestAccess
{
	static wxImage Render(CKadContactHistogram &panel, const wxSize &size)
	{
		wxBitmap bitmap(size.x, size.y, 24);
		wxMemoryDC dc(bitmap);
		panel.Draw(dc, size);
		dc.SelectObject(wxNullBitmap);
		return bitmap.ConvertToImage();
	}
};

static void Check(bool condition, const char *message)
{
	if (!condition) {
		throw std::runtime_error(message);
	}
}
static size_t Pixels(const wxImage &image, const wxColour &colour)
{
	size_t count = 0;
	for (int y = 0; y < image.GetHeight(); ++y) {
		for (int x = 0; x < image.GetWidth(); ++x) {
			if (image.GetRed(x, y) == colour.Red() && image.GetGreen(x, y) == colour.Green() &&
				image.GetBlue(x, y) == colour.Blue()) {
				++count;
			}
		}
	}
	return count;
}
static void VerifyRendering()
{
	wxFrame frame(nullptr, wxID_ANY, "Kad histogram test");
	CKadContactHistogram panel(&frame);
	panel.SetBackgroundColour(*wxWHITE);
	panel.SetForegroundColour(*wxBLACK);
	Kademlia::ContactDistribution populated;
	populated.contacts.fill(1000);
	populated.verified.fill(500);
	populated.subnets = 250;
	panel.SetDistribution(populated, true);
	for (const wxSize size : { wxSize(240, 240), wxSize(740, 240) }) {
		const auto image = KadHistogramTestAccess::Render(panel, size);
		Check(Pixels(image, wxColour(70, 130, 210)) > 0, "Total-contact bars missing");
		Check(Pixels(image, wxColour(35, 160, 90)) > 0, "Verified-contact bars missing");
	}
	wxFont font = panel.GetFont();
	font.SetPointSize(24);
	panel.SetFont(font);
	Check(Pixels(KadHistogramTestAccess::Render(panel, wxSize(740, 360)), wxColour(35, 160, 90)) > 0,
		"Large fonts hide the chart");
	// Degenerate widths and heights must clip safely rather than divide by zero.
	KadHistogramTestAccess::Render(panel, wxSize(20, 20));
	panel.SetDistribution(populated, false);
	auto unavailable = KadHistogramTestAccess::Render(panel, wxSize(240, 240));
	Check(Pixels(unavailable, wxColour(70, 130, 210)) == 0, "Unavailable data displays stale bars");
	panel.SetDistribution({}, true);
	auto empty = KadHistogramTestAccess::Render(panel, wxSize(740, 360));
	Check(Pixels(empty, wxColour(35, 160, 90)) == 0, "Stopped Kad displays stale verified contacts");
}
int main(int argc, char **argv)
{
	if (!wxEntryStart(argc, argv)) {
		return 77;
	}
	int result = 0;
	if (!wxTheApp->CallOnInit()) {
		result = 1;
	} else {
		try {
			VerifyRendering();
			std::cout << "Histogram render checks passed\n";
		} catch (const std::exception &error) {
			std::cerr << error.what() << '\n';
			result = 1;
		}
		wxTheApp->OnExit();
	}
	wxEntryCleanup();
	return result;
}
