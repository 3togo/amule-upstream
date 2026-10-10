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

#include "KadLookupView.h"
#include <stdexcept>
#include <iostream>

class LookupTestApp : public wxApp
{
public:
	bool OnInit() override { return true; }
};
wxIMPLEMENT_APP_NO_MAIN(LookupTestApp);
static void Check(bool value, const char *message)
{
	if (!value) {
		throw std::runtime_error(message);
	}
}
static void VerifyLifecycle()
{
	auto *frame = new wxFrame(nullptr, wxID_ANY, "Lookup lifecycle test");
	auto *button = new wxButton(frame, wxID_ANY, "Request");
	auto *view = new CKadLookupView(frame);
	view->Show();
	Check(!view->IsModal(), "Diagnostics blocks connection controls");
	button->Disable();
	(new CKadLookupReply(view, button))->AbortPendingRequest();
	Check(button->IsEnabled(), "Abort prevents retry");
	Check(view->GetSnapshot().Contains("Connection lost"), "Abort leaves stale snapshot");
	CECPacket unsupported(EC_OP_FAILED);
	button->Disable();
	(new CKadLookupReply(view, button))->HandlePacket(&unsupported);
	Check(button->IsEnabled(), "Unsupported core prevents retry");
	Check(view->GetSnapshot().Contains("does not support"), "Unsupported core is not explained");
	CECPacket valid(EC_OP_GET_KAD_LOOKUPS);
	CECEmptyTag lookup(EC_TAG_KAD_LOOKUP);
	lookup.AddTag(CECTag(EC_TAG_KAD_LOOKUP_TARGET, wxString("target")));
	lookup.AddTag(CECTag(EC_TAG_KAD_LOOKUP_TYPE, uint32_t(3)));
	lookup.AddTag(CECTag(EC_TAG_KAD_LOOKUP_ACTIVE, uint8_t(1)));
	CECEmptyTag peer(EC_TAG_KAD_LOOKUP_PEER);
	peer.AddTag(CECTag(EC_TAG_KAD_LOOKUP_PEER_IP, uint32_t(0x01020304)));
	peer.AddTag(CECTag(EC_TAG_KAD_LOOKUP_PEER_PORT, uint16_t(4665)));
	peer.AddTag(CECTag(EC_TAG_KAD_LOOKUP_PEER_VERSION, uint8_t(8)));
	peer.AddTag(CECTag(EC_TAG_KAD_LOOKUP_PEER_PENDING, uint8_t(1)));
	uint8_t distance[16] = {};
	distance[15] = 7;
	peer.AddTag(CECTag(EC_TAG_KAD_LOOKUP_PEER_DISTANCE, 16, distance));
	lookup.AddTag(peer);
	valid.AddTag(lookup);
	(new CKadLookupReply(view, button))->HandlePacket(&valid);
	Check(view->GetSnapshot().Contains("target") && view->GetSnapshot().Contains("Keyword") &&
			view->GetSnapshot().Contains("1.2.3.4:4665") &&
			view->GetSnapshot().Contains("Kad version 8") &&
			view->GetSnapshot().Contains("00000000000000000000000000000007"),
		"Valid reply missing");
	CECPacket malformed(EC_OP_GET_KAD_LOOKUPS);
	malformed.AddTag(CECTag(EC_TAG_KAD_LOOKUP, uint32_t(42)));
	(new CKadLookupReply(view, button))->HandlePacket(&malformed);
	Check(view->GetSnapshot().Contains("Invalid Kad lookup"), "Malformed reply interpreted as text");
	auto *closedReply = new CKadLookupReply(view, button);
	delete view; // response may arrive after the parent has destroyed the view
	closedReply->HandlePacket(&valid);
	Check(button->IsEnabled(), "Closed view prevents retry");
	view = new CKadLookupView(frame);
	auto *parentReply = new CKadLookupReply(view, button);
	delete frame;
	parentReply->AbortPendingRequest(); // both weak references must already be invalid
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
			VerifyLifecycle();
		} catch (const std::exception &e) {
			std::cerr << e.what() << '\n';
			result = 1;
		}
		wxTheApp->OnExit();
	}
	wxEntryCleanup();
	return result;
}
