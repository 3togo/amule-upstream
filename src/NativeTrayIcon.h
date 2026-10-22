// This file is part of the aMule Project.
// Copyright (c) 2026 aMule Team ( https://amule-org.github.io )
// Licensed under the GNU GPL, version 2 or later; see LICENSE.md.
#ifndef AMULE_NATIVE_TRAY_ICON_H
#define AMULE_NATIVE_TRAY_ICON_H

#include "config.h"
#include <wx/taskbar.h>

// wx's AppIndicator backend already negotiates its icon size with the host.
// Use a small native adapter only where wx hides the APIs we need: GtkStatusIcon's
// requested slot size and NSStatusItem's template image. Keep wx menu dispatch.
#if !defined(WITH_LIBAYATANA_APPINDICATOR) && \
	(defined(__WXOSX_COCOA__) || (defined(__WXGTK218__) && !defined(__WXGTK4__) && \
					     !(defined(wxUSE_APPINDICATOR) && wxUSE_APPINDICATOR)))
#define AMULE_NATIVE_TRAY_ICON
#include <memory>
class MuleTrayIconBase : public wxTaskBarIcon
{
public:
	MuleTrayIconBase();
	~MuleTrayIconBase() override;
	bool SetIcon(const wxBitmapBundle &icon, const wxString &tooltip = wxEmptyString) override;
	bool RemoveIcon() override;
	bool PopupMenu(wxMenu *menu) override;
	bool IsIconInstalled() const;

private:
	struct Impl;
	std::unique_ptr<Impl> m_native;
};
#else
using MuleTrayIconBase = wxTaskBarIcon;
#endif
#endif
