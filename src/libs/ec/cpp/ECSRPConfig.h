// Copyright (c) 2026 aMule Team. GPL-2.0-or-later.
#ifndef ECSRPCONFIG_H
#define ECSRPCONFIG_H
#include <wx/config.h>
namespace ECSRP
{
// Write and flush the verifier before removing the old credential. On failure
// to commit, restore the in-memory config too (wxFileConfig flushes on exit).
bool StoreCredential(wxConfigBase &cfg, const wxString &digest, wxString &verifier, wxString &error);
bool LoadCredential(wxConfigBase &cfg, wxString &verifier, wxString &error);
} // namespace ECSRP
#endif
