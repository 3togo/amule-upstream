// Copyright (c) 2026 aMule Team. GPL-2.0-or-later.
#ifndef EC_CLIENT_PASSWORD_H
#define EC_CLIENT_PASSWORD_H

#include <wx/config.h>

namespace ECClientPassword
{
// The server has already committed next. Always update the in-memory reconnect
// credential, but only replace a saved credential for this exact connection.
// In particular, rotating a password must not opt a user into saving passwords.
inline bool Commit(
	wxConfigBase &cfg, const wxString &host, int port, wxString &sessionPassword, const wxString &next)
{
	const bool remembered = cfg.Read("/EC/Host", wxString()) == host &&
				cfg.Read("/EC/Port", 4712L) == port && !sessionPassword.IsEmpty() &&
				cfg.Read("/EC/Password", wxString()) == sessionPassword;
	sessionPassword = next;
	return !remembered || (cfg.Write("/EC/Password", next) && cfg.Flush());
}
} // namespace ECClientPassword

#endif
