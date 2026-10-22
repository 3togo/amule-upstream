// This file is part of the aMule Project.
// Copyright (c) 2026 aMule Team ( https://amule-org.github.io )
// Licensed under the GNU GPL, version 2 or later; see LICENSE.md.
#include "NativeTrayIcon.h"

#if defined(AMULE_NATIVE_TRAY_ICON) && defined(__WXGTK__)
#include <wx/bmpbndl.h>
#include <wx/bitmap.h>
#include <gtk/gtk.h>

// GtkStatusIcon is deprecated but remains the X11 fallback when neither aMule
// nor wx was built with AppIndicator. Do not suppress deprecations elsewhere.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
struct MuleTrayIconBase::Impl
{
	GtkStatusIcon *status = nullptr;
	wxBitmapBundle artwork;

	bool Render(int size)
	{
		if (!artwork.IsOk() || size <= 0) {
			return false;
		}
		// GTK requests device pixels, including on scaled displays. Do not multiply
		// by the main window's scale (the tray can be on another monitor).
		const wxBitmap bitmap = artwork.GetBitmap(wxSize(size, size));
		if (bitmap.IsOk()) {
			gtk_status_icon_set_from_pixbuf(status, bitmap.GetPixbuf());
			return true;
		}
		return false;
	}
};

MuleTrayIconBase::MuleTrayIconBase()
: m_native(new Impl)
{
}
MuleTrayIconBase::~MuleTrayIconBase()
{
	RemoveIcon();
}

bool MuleTrayIconBase::SetIcon(const wxBitmapBundle &icon, const wxString &tooltip)
{
	if (!icon.IsOk()) {
		return false;
	}
	m_native->artwork = icon;
	if (!m_native->status) {
		m_native->status = gtk_status_icon_new();
		gtk_status_icon_set_title(m_native->status, "aMule");
		g_signal_connect(m_native->status,
			"size-changed",
			G_CALLBACK(+[](GtkStatusIcon *, gint size, gpointer data) -> gboolean {
				return static_cast<Impl *>(data)->Render(size);
			}),
			m_native.get());
		g_signal_connect(m_native->status,
			"activate",
			G_CALLBACK(+[](GtkStatusIcon *, gpointer data) {
				auto *owner = static_cast<MuleTrayIconBase *>(data);
				wxTaskBarIconEvent event(wxEVT_TASKBAR_LEFT_DCLICK, owner);
				owner->SafelyProcessEvent(event);
			}),
			this);
		g_signal_connect(m_native->status,
			"popup-menu",
			G_CALLBACK(+[](GtkStatusIcon *, guint, guint, gpointer data) {
				auto *owner = static_cast<MuleTrayIconBase *>(data);
				wxTaskBarIconEvent event(wxEVT_TASKBAR_CLICK, owner);
				owner->SafelyProcessEvent(event);
			}),
			this);
	}
	int size = gtk_status_icon_get_size(m_native->status);
	if (size <= 0) {
		size = icon.GetDefaultSize().y;
	}
	if (!m_native->Render(size)) {
		return false;
	}
	gtk_status_icon_set_tooltip_text(m_native->status, tooltip.utf8_str());
	gtk_status_icon_set_visible(m_native->status, TRUE);
	return true;
}

bool MuleTrayIconBase::RemoveIcon()
{
	if (m_native->status) {
		g_signal_handlers_disconnect_by_data(m_native->status, this);
		g_signal_handlers_disconnect_by_data(m_native->status, m_native.get());
		gtk_status_icon_set_visible(m_native->status, FALSE);
		g_object_unref(m_native->status);
		m_native->status = nullptr;
	}
	m_native->artwork = wxBitmapBundle();
	return true;
}

bool MuleTrayIconBase::IsIconInstalled() const
{
	return m_native->status != nullptr;
}
bool MuleTrayIconBase::PopupMenu(wxMenu *menu)
{
	return wxTaskBarIcon::PopupMenu(menu);
}
#pragma GCC diagnostic pop
#endif
