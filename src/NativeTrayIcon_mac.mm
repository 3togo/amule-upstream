// This file is part of the aMule Project.
// Copyright (c) 2026 aMule Team ( https://amule-org.github.io )
// Licensed under the GNU GPL, version 2 or later; see LICENSE.md.
#include "NativeTrayIcon.h"
#if defined(AMULE_NATIVE_TRAY_ICON) && defined(__WXOSX_COCOA__)
#import <AppKit/AppKit.h>
#include <algorithm>
#include <wx/bmpbndl.h>
#include <wx/bitmap.h>
#include <wx/menu.h>

@interface AMuleTrayTarget : NSObject {
@public
	MuleTrayIconBase *owner;
}
- (void)clicked:(id)sender;
@end
@implementation AMuleTrayTarget
- (void)clicked:(id)sender
{
	(void)sender;
	wxTaskBarIconEvent event(wxEVT_TASKBAR_CLICK, owner);
	owner->SafelyProcessEvent(event);
}
@end

struct MuleTrayIconBase::Impl
{
	NSStatusItem *status = nil;
	AMuleTrayTarget *target = nil;
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
	if (!m_native->status) {
		m_native->status = [[[NSStatusBar systemStatusBar]
			statusItemWithLength:NSSquareStatusItemLength] retain];
		m_native->target = [[AMuleTrayTarget alloc] init];
		m_native->target->owner = this;
		m_native->status.button.target = m_native->target;
		m_native->status.button.action = @selector(clicked:);
		[m_native->status.button sendActionOn:NSEventMaskLeftMouseDown | NSEventMaskRightMouseDown];
	}
	const CGFloat points = std::max<CGFloat>(1, [NSStatusBar systemStatusBar].thickness - 4);
	const CGFloat scale =
		m_native->status.button.window.backingScaleFactor ?: [NSScreen mainScreen].backingScaleFactor;
	const int pixels = wxRound(points * std::max<CGFloat>(1, scale));
	const wxBitmap bitmap = icon.GetBitmap(wxSize(pixels, pixels));
	if (!bitmap.IsOk()) {
		return false;
	}
	// wx's status backend wraps the bitmap in a new non-template NSImage. Own
	// the status item so the image AppKit actually displays carries the flag.
	NSImage *image = [bitmap.GetNSImage() copy];
	image.size = NSMakeSize(points, points);
	[image setTemplate:YES];
	m_native->status.button.image = image;
	[image release];
	m_native->status.button.toolTip = [NSString stringWithUTF8String:tooltip.utf8_str()];
	return true;
}

bool MuleTrayIconBase::RemoveIcon()
{
	if (m_native->status) {
		m_native->status.button.target = nil;
		[[NSStatusBar systemStatusBar] removeStatusItem:m_native->status];
		[m_native->status release];
		[m_native->target release];
		m_native->status = nil;
		m_native->target = nil;
	}
	return true;
}

bool MuleTrayIconBase::IsIconInstalled() const
{
	return m_native->status != nil;
}
bool MuleTrayIconBase::PopupMenu(wxMenu *menu)
{
	if (!m_native->status || !menu) {
		return false;
	}
	wxEvtHandler *previous = menu->GetEventHandler();
	menu->SetEventHandler(this);
	menu->UpdateUI();
	[m_native->status popUpStatusItemMenu:(NSMenu *)menu->GetHMenu()];
	menu->SetEventHandler(previous);
	return true;
}
#endif
