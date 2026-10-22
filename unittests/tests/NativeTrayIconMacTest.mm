// This file is part of the aMule Project.
// Copyright (c) 2026 aMule Team. Licensed under the GNU GPL v2 or later.
#include "NativeTrayIcon.h"
#if defined(AMULE_NATIVE_TRAY_ICON) && defined(__WXOSX_COCOA__)
#import <AppKit/AppKit.h>
#import <objc/runtime.h>
#include <wx/app.h>
#include <wx/bmpbndl.h>
#include <wx/image.h>
#include <wx/menu.h>
#include <iostream>
#include <stdexcept>

// Intercept the public factory to inspect the actual displayed status item.
static NSStatusItem *status;
static IMP originalFactory;
static NSStatusItem *CaptureStatus(id bar, SEL method, CGFloat length)
{
	status = reinterpret_cast<NSStatusItem *(*)(id, SEL, CGFloat)>(originalFactory)(bar, method, length);
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
class TestTray : public MuleTrayIconBase
{
public:
	int menus = 0;
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
		return 1;
	}
	if (!wxTheApp->CallOnInit()) {
		wxEntryCleanup();
		return 1;
	}
	Method factory = class_getInstanceMethod(
		object_getClass([NSStatusBar systemStatusBar]), @selector(statusItemWithLength:));
	originalFactory = method_setImplementation(factory, reinterpret_cast<IMP>(CaptureStatus));
	int result = 0;
	try {
		wxImage image(22, 22);
		image.SetRGB(wxRect(0, 0, 22, 22), 0, 0, 0);
		const auto bundle = wxBitmapBundle::FromBitmap(wxBitmap(image));
		TestTray tray;
		require(!tray.SetIcon(wxBitmapBundle()), "invalid bundle accepted");
		require(tray.SetIcon(bundle, "template test"), "SetIcon failed");
		require(tray.IsIconInstalled() && status && [status.button.image isTemplate],
			"displayed image is not a template");
		require([status.button.toolTip isEqualToString:@"template test"], "tooltip lost");
		require(status.button.image.size.height > 0 &&
				status.button.image.size.height < [NSStatusBar systemStatusBar].thickness,
			"menu bar image has wrong logical size");
		[status.button performClick:nil];
		require(tray.menus == 1, "native click lost wx menu dispatch");
		NSStatusItem *removed = [status retain];
		tray.RemoveIcon();
		require(!tray.IsIconInstalled() && removed.button.target == nil,
			"removed icon retained callbacks");
		[removed release];
		require(tray.SetIcon(bundle, "restored") && [status.button.image isTemplate],
			"restore lost template flag");
		tray.RemoveIcon();
		tray.RemoveIcon();
		std::cout << "PASS: displayed template image, native menu dispatch and remove/restore\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		result = 1;
	}
	method_setImplementation(factory, originalFactory);
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
