// Copyright (c) 2026 aMule Team. GPL-2.0-or-later.
#include "ECSRPConfig.h"
#include "ECSRP.h"

namespace ECSRP
{
namespace
{
const char *const KEY = "/ExternalConnect/ECPasswordVerifier";
const char *const LEGACY = "/ExternalConnect/ECPassword";
void Cleanup(wxConfigBase &cfg, wxString &error)
{
	if (cfg.HasEntry(LEGACY)) {
		const wxString old = cfg.Read(LEGACY, wxString());
		if (!cfg.DeleteEntry(LEGACY) || !cfg.Flush()) {
			cfg.Write(LEGACY, old);
			error = "Could not remove legacy ECPassword; cleanup will be retried on restart.";
		}
	}
}
} // namespace
bool StoreCredential(wxConfigBase &cfg, const wxString &digest, wxString &verifier, wxString &error)
{
	error.clear();
	try {
		const wxString next =
			digest.IsEmpty()
				? wxString()
				: wxString::FromUTF8(
					  Verifier::Create(std::string(digest.utf8_str())).Encode().c_str());
		const bool existed = cfg.HasEntry(KEY);
		const wxString previous = cfg.Read(KEY, wxString());
		if (!cfg.Write(KEY, next) || !cfg.Flush()) {
			if (existed)
				cfg.Write(KEY, previous);
			else
				cfg.DeleteEntry(KEY);
			error = "Could not persist ECPasswordVerifier; previous credential retained.";
			return false;
		}
		verifier = next;
		Cleanup(cfg, error);
		return true;
	} catch (const std::exception &) {
		error = "Could not generate ECPasswordVerifier; previous credential retained.";
		return false;
	}
}
bool LoadCredential(wxConfigBase &cfg, wxString &verifier, wxString &error)
{
	error.clear();
	verifier.clear();
	if (cfg.HasEntry(KEY)) {
		verifier = cfg.Read(KEY, wxString());
		try {
			if (!verifier.IsEmpty())
				Verifier::Decode(std::string(verifier.utf8_str()));
		} catch (const std::exception &) {
			error = "Invalid or unsupported ECPasswordVerifier; refusing external connections.";
			return false;
		}
		Cleanup(cfg, error);
		return true;
	}
	const wxString legacy = cfg.Read(LEGACY, wxString());
	if (legacy.IsEmpty())
		return true;
	return StoreCredential(cfg, legacy, verifier, error);
}
} // namespace ECSRP
