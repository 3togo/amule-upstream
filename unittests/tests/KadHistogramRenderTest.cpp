// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later
#include <wx/wx.h>
#include "KadContactHistogram.h"
#include <iostream>
#include <stdexcept>
#include <cstdlib>

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
		// Test sizes describe DIP layouts, while this bitmap/DC uses pixels.
		const wxSize pixels = panel.FromDIP(size);
		wxBitmap bitmap(pixels.x, pixels.y, 24);
		wxMemoryDC dc(bitmap);
		panel.Draw(dc, pixels);
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
static size_t ColourRuns(const wxImage &image, const wxColour &colour)
{
	size_t runs = 0;
	bool previous = false;
	for (int x = 0; x < image.GetWidth(); ++x) {
		bool found = false;
		for (int y = 0; y < image.GetHeight(); ++y) {
			if (image.GetRed(x, y) == colour.Red() && image.GetGreen(x, y) == colour.Green() &&
				image.GetBlue(x, y) == colour.Blue()) {
				found = true;
				break;
			}
		}
		if (found && !previous) {
			++runs;
		}
		previous = found;
	}
	return runs;
}
static void SaveRender(const wxImage &image, const char *name)
{
	const char *directory = std::getenv("AMULE_HISTOGRAM_RENDER_DIR");
	if (directory) {
		Check(image.SaveFile(wxString::FromUTF8(directory) + "/" + name, wxBITMAP_TYPE_PNG),
			"Cannot save render artifact");
	}
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
	populated.hasLocalID = true;
	populated.localID = 0x80000000;
	panel.SetDistribution(populated, Kademlia::ContactDistributionState::Available);
	for (const wxSize size : { wxSize(240, 240), wxSize(740, 240) }) {
		const auto image = KadHistogramTestAccess::Render(panel, size);
		SaveRender(image, size.x < 300 ? "narrow-light.png" : "wide-light.png");
		Check(Pixels(image, wxColour(70, 130, 210)) > 0, "Total-contact bars missing");
		Check(Pixels(image, wxColour(35, 160, 90)) > 0, "Verified-contact bars missing");
	}
	// Dark GTK palette, with the native widget font and foreground/background.
	panel.SetBackgroundColour(wxColour(32, 32, 32));
	panel.SetForegroundColour(wxColour(240, 240, 240));
	const auto dark = KadHistogramTestAccess::Render(panel, wxSize(740, 240));
	SaveRender(dark, "wide-dark.png");
	Check(Pixels(dark, wxColour(70, 130, 210)) > 0, "Dark theme total bars missing");
	Check(Pixels(dark, wxColour(240, 240, 240)) > 0, "Dark theme marker/text missing");
	panel.SetDistribution({}, Kademlia::ContactDistributionState::Loading);
	Check(Pixels(KadHistogramTestAccess::Render(panel, wxSize(240, 240)), wxColour(70, 130, 210)) == 0,
		"Loading displays stale bars");
	// Two distinct bins within one former six-bit bin must remain separated at wide widths.
	Kademlia::ContactDistribution clustered;
	clustered.contacts[2048] = 10;
	clustered.contacts[2100] = 20;
	panel.SetDistribution(clustered, Kademlia::ContactDistributionState::Available);
	const auto clusteredImage = KadHistogramTestAccess::Render(panel, wxSize(740, 240));
	Check(ColourRuns(clusteredImage, wxColour(70, 130, 210)) == 2,
		"Distinct bins within one former six-bit bin collapse at wide widths");
	clustered.subnets = 2;
	clustered.verified[2048] = 5;
	clustered.verified[2100] = 10;
	clustered.hasLocalID = true;
	clustered.localID = 0x82000000;
	panel.SetDistribution(clustered, Kademlia::ContactDistributionState::Available);
	const auto markedImage = KadHistogramTestAccess::Render(panel, wxSize(740, 240));
	Check(Pixels(markedImage, wxColour(240, 240, 240)) >
			Pixels(clusteredImage, wxColour(240, 240, 240)) + 50,
		"Local KadID marker is missing or has insufficient contrast");
	SaveRender(markedImage, "clustered-dark.png");
	panel.SetDistribution(populated, Kademlia::ContactDistributionState::Available);
	wxFont font = panel.GetFont();
	font.SetPointSize(24);
	panel.SetFont(font);
	// Allocate space relative to the enlarged font, including GTK font-DPI
	// scaling which does not necessarily change FromDIP() on this backend.
	const wxSize largeSize(panel.GetCharWidth() * 70, panel.GetCharHeight() * 8);
	const auto largeFont = KadHistogramTestAccess::Render(panel, largeSize);
	SaveRender(largeFont, "large-font.png");
	Check(Pixels(largeFont, wxColour(35, 160, 90)) > 0, "Large fonts hide the chart");
	// Degenerate widths and heights must clip safely rather than divide by zero.
	KadHistogramTestAccess::Render(panel, wxSize(20, 20));
	panel.SetDistribution(populated, Kademlia::ContactDistributionState::Unsupported);
	auto unavailable = KadHistogramTestAccess::Render(panel, wxSize(240, 240));
	Check(Pixels(unavailable, wxColour(70, 130, 210)) == 0, "Unavailable data displays stale bars");
	panel.SetDistribution({}, Kademlia::ContactDistributionState::Available);
	auto empty = KadHistogramTestAccess::Render(panel, wxSize(740, 360));
	Check(Pixels(empty, wxColour(35, 160, 90)) == 0, "Stopped Kad displays stale verified contacts");
}
int main(int argc, char **argv)
{
	wxInitAllImageHandlers();
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
