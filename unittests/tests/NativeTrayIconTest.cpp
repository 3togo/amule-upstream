// This file is part of the aMule Project.
// Copyright (c) 2026 aMule Team. Licensed under the GNU GPL v2 or later.
#include "NativeTrayIcon.h"
#if defined(AMULE_NATIVE_TRAY_ICON) && defined(__WXGTK__)
#include <wx/app.h>
#include <wx/bmpbndl.h>
#include <wx/image.h>
#include <wx/menu.h>
#include <gtk/gtk.h>
#include <iostream>
#include <stdexcept>
#include <wx/utils.h>

// Observe the actual production GtkStatusIcon without exposing test accessors.
static GtkStatusIcon *status;
extern "C" GtkStatusIcon *__real_gtk_status_icon_new();
extern "C" GtkStatusIcon *__wrap_gtk_status_icon_new()
{
	status = __real_gtk_status_icon_new();
	return status;
}
class TestApp : public wxApp
{
public:
	bool OnInit() override { return true; }
};
wxIMPLEMENT_APP_NO_MAIN(TestApp);
static void require(bool ok, const char *message)
{
	if (!ok) {
		throw std::runtime_error(message);
	}
}
// This mode talks to an actual X11 tray manager. Do not emit size-changed:
// embedding and slot allocation must drive the production callback themselves.
static void CheckLiveTray()
{
	long expected = 0;
	const wxString requested = wxGetenv("AMULE_TRAY_TEST_SIZE");
	if (requested.empty()) {
		return;
	}
	require(requested.ToLong(&expected) && expected > 0, "invalid live tray size");
	for (int attempt = 0; attempt < 250; ++attempt) {
		wxYield();
		GdkPixbuf *pixels = gtk_status_icon_get_pixbuf(status);
		if (gtk_status_icon_is_embedded(status) && gtk_status_icon_get_size(status) == expected &&
			pixels && gdk_pixbuf_get_width(pixels) == expected &&
			gdk_pixbuf_get_height(pixels) == expected) {
			std::cout << "PASS: live X11 tray embedded and rendered at " << expected << "px\n";
			return;
		}
		wxMilliSleep(20);
	}
	throw std::runtime_error("live tray did not embed and render at requested slot size");
}

class TestTray : public MuleTrayIconBase
{
public:
	int menus = 0;
	int clicks = 0;
	TestTray()
	{
		Bind(wxEVT_TASKBAR_LEFT_DCLICK, [this](wxTaskBarIconEvent &) { ++clicks; });
	}
	wxMenu *CreatePopupMenu() override { return new wxMenu; }
	bool PopupMenu(wxMenu *menu) override
	{
		require(menu != nullptr, "missing menu");
		++menus;
		return true;
	}
};
int main(int argc, char **argv)
{
	if (!wxEntryStart(argc, argv)) {
		return wxGetenv("AMULE_ICON_TEST_REQUIRE_GUI") ? 1 : 77;
	}
	if (!wxTheApp->CallOnInit()) {
		wxEntryCleanup();
		return 1;
	}
	int result = 0;
	try {
		wxImage image(22, 22);
		image.SetRGB(wxRect(0, 0, 22, 22), 23, 89, 156);
		const auto bundle = wxBitmapBundle::FromBitmap(wxBitmap(image));
		TestTray tray;
		require(!tray.IsIconInstalled(), "icon installed before SetIcon");
		require(!tray.SetIcon(wxBitmapBundle()), "invalid bundle accepted");
		require(tray.SetIcon(bundle, "test tooltip"), "SetIcon failed");
		require(tray.IsIconInstalled() && status, "missing native icon");
		CheckLiveTray();
		for (int size : { 16, 22, 32, 48, 64, 22 }) {
			gboolean handled = FALSE;
			g_signal_emit_by_name(status, "size-changed", size, &handled);
			GdkPixbuf *pixels = gtk_status_icon_get_pixbuf(status);
			require(handled && pixels && gdk_pixbuf_get_width(pixels) == size &&
					gdk_pixbuf_get_height(pixels) == size,
				"tray slot size ignored");
		}
		g_signal_emit_by_name(status, "activate");
		g_signal_emit_by_name(status, "popup-menu", 3u, 0u);
		require(tray.clicks == 1 && tray.menus == 1, "native events lost wx dispatch");
		g_object_ref(status); // Retain to check callbacks cannot reach a removed owner.
		GtkStatusIcon *removed = status;
		tray.RemoveIcon();
		require(!tray.IsIconInstalled() && !gtk_status_icon_get_visible(removed),
			"RemoveIcon left icon visible");
		g_signal_emit_by_name(removed, "activate");
		require(tray.clicks == 1, "removed icon kept callbacks");
		g_object_unref(removed);
		require(tray.SetIcon(bundle, "restored"), "restore failed");
		require(tray.IsIconInstalled() && gtk_status_icon_get_visible(status),
			"restored icon hidden");
		CheckLiveTray();
		tray.RemoveIcon();
		tray.RemoveIcon();
		std::cout << "PASS: native slot sizing, wx events, remove/restore and callback lifetime\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		result = 1;
	}
	wxTheApp->OnExit();
	wxEntryCleanup();
	return result;
}
#else
int main()
{
	return 77;
}
#endif
