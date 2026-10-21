// This file is part of the aMule Project, licensed under GPL-2.0-or-later.
// Copyright (c) 2026 aMule Team ( https://amule-org.github.io )

#ifndef AMULE_TRAYICONBUNDLE_H
#define AMULE_TRAYICONBUNDLE_H

#include <algorithm>
#include <wx/bmpbndl.h>
#include <wx/colour.h>
#include <wx/image.h>

namespace MuleTray
{
// wxTaskBarIcon retains this bundle and requests bitmaps at the native tray's scale,
// including after a DPI change. Compose at that size, never upscale a finished bar.
class IconBundleImpl : public wxBitmapBundleImpl
{
public:
	IconBundleImpl(const wxBitmapBundle &artwork, unsigned int percent, const wxColour &colour)
	: m_artwork(artwork)
	, m_percent(std::min(percent, 100u))
	, m_colour(colour)
	{
	}

	wxSize GetDefaultSize() const override { return m_artwork.GetDefaultSize(); }

	wxSize GetPreferredBitmapSizeAtScale(double scale) const override
	{
		return m_artwork.GetPreferredBitmapSizeAtScale(scale);
	}

	wxBitmap GetBitmap(const wxSize &size) override
	{
		if (m_bitmap.IsOk() && m_bitmap.GetSize() == size) {
			return m_bitmap;
		}
		const wxBitmap base = m_artwork.GetBitmap(size);
		if (!base.IsOk()) {
			return wxNullBitmap;
		}
		// Convert and copy each time: neither the original artwork nor an earlier
		// rendition may accumulate opaque pixels as the transfer rate changes.
		wxImage image = base.ConvertToImage().Copy();
		if (!image.HasAlpha()) {
			image.InitAlpha();
		}
		const int width = image.GetWidth();
		const int height = image.GetHeight();
		// Integer arithmetic avoids dropping a row at exact boundaries (e.g.
		// 29% of 100 pixels); widen first so large images cannot overflow.
		const int barHeight = static_cast<int>((static_cast<long long>(height) * m_percent) / 100);
		// The artwork reserves the final two columns of its 22-pixel canvas.
		const int barWidth = std::max(1, wxRound(width * (2.0 / 22.0)));
		for (int y = height - barHeight; y < height; ++y) {
			for (int x = width - barWidth; x < width; ++x) {
				// wxMSW DC drawing does not set alpha on transparent pixels.
				image.SetRGB(x, y, m_colour.Red(), m_colour.Green(), m_colour.Blue());
				image.SetAlpha(x, y, wxALPHA_OPAQUE);
			}
		}
		m_bitmap = wxBitmap(image);
		return m_bitmap;
	}

private:
	wxBitmapBundle m_artwork;
	unsigned int m_percent;
	wxColour m_colour;
	wxBitmap m_bitmap;
};

inline wxBitmapBundle MakeIconBundle(
	const wxBitmapBundle &artwork, unsigned int percent, const wxColour &colour)
{
	if (!artwork.IsOk()) {
		return wxBitmapBundle();
	}
	return wxBitmapBundle::FromImpl(new IconBundleImpl(artwork, percent, colour));
}
} // namespace MuleTray

#endif // AMULE_TRAYICONBUNDLE_H
