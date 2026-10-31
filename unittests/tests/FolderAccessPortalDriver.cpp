//
// This file is part of the aMule Project.
//
// Copyright (c) 2003-2026 aMule Team ( https://amule-org.github.io )
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
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
//

// Small GUI client for the fake-portal integration test; never uses a real profile.
#include <wx/app.h>
#include <cstdio>
#include <csignal>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <sys/resource.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <cstddef>
#include <wx/file.h>
#include <wx/log.h>
#include <wx/textfile.h>
#include "CamuleFileConfig.h"
#include "Preferences.h"
#include "FolderAccess.h"

wxString CPreferences::s_configDir;
CPath CPreferences::s_tempdir;
CPath CPreferences::s_incomingdir;
bool CPreferences::s_onlineSig = false;
CPath CPreferences::s_OSDirectory;

class PortalTestApp : public wxApp
{
public:
	bool OnInit() override { return true; }
};
wxIMPLEMENT_APP_NO_MAIN(PortalTestApp);

bool CheckAtomicSave(const wxString &profile)
{
	const wxString path = profile + "/atomic.conf";
	{
		wxFile original;
		if (!original.Create(path, true, 0600) || !original.Write("old=1\n")) {
			return false;
		}
	}
	CamuleFileConfig config("", "", path);
	config.Write("value", wxString('x', 8192));
	struct rlimit previous = {};
	if (getrlimit(RLIMIT_FSIZE, &previous) != 0) {
		return false;
	}
	struct rlimit limited = previous;
	limited.rlim_cur = 8;
	std::signal(SIGXFSZ, SIG_IGN);
	if (setrlimit(RLIMIT_FSIZE, &limited) != 0) {
		return false;
	}
	const bool rejected = !config.Flush();
	if (setrlimit(RLIMIT_FSIZE, &previous) != 0) {
		return false;
	}
	wxString originalContents;
	{
		wxFile original(path);
		if (!original.ReadAll(&originalContents)) {
			return false;
		}
	}
	const bool retry = config.Flush();
	return rejected && originalContents == "old=1\n" && retry && FolderAccess::BackupConfig(path) &&
	       CPath::BackupFile(CPath(path), ".credits.bak");
}

bool CheckTextSave(const wxString &profile)
{
	const wxString path = profile + "/addresses.dat";
	const wxString contents = "https://example.test/servers.met\n";
	if (!FolderAccess::SaveText(path, "old\n") || !FolderAccess::SaveText(path, contents))
		return false;
	{
		wxFile file(path);
		wxString saved;
		if (!file.ReadAll(&saved) || saved != contents)
			return false;
	}
	if (!FolderAccess::SaveText(path, wxEmptyString))
		return false;
	wxFile empty(path);
	if (!empty.IsOpened() || empty.Length() != 0)
		return false;
	empty.Close();
	// Preserve mixed line endings, Unicode, and a final line without a terminator.
	const wxString mixed = wxString::FromUTF8("first\r\n\u672b\u884c");
	if (!FolderAccess::SaveText(path, mixed))
		return false;
	wxTextFile lines(path);
	if (!lines.Open() || !FolderAccess::SaveTextFile(lines))
		return false;
	wxFile saved(path);
	wxString actual;
	if (!saved.ReadAll(&actual, wxConvUTF8) || actual != mixed)
		return false;
	saved.Close();
	return FolderAccess::SaveText(path, wxEmptyString);
}

// Fail the rename after writing the complete temporary file. A copy fallback
// could still open and overwrite the existing destination.
bool CheckRejectedPromotion(const wxString &profile)
{
	const wxString path = profile + "/atomic.conf";
	const wxString backup = path + ".bak";
	for (const auto &name : { path, backup }) {
		wxFile file;
		if (!file.Create(name, true, 0600) || !file.Write("old=1\n"))
			return false;
	}
	CamuleFileConfig config("", "", path);
	config.Write("value", "new contents");
	std::vector<sock_filter> filter = { BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)) };
	const unsigned operations[] = {
#ifdef __NR_rename
		__NR_rename,
#endif
		__NR_renameat,
		__NR_renameat2
	};
	for (const unsigned operation : operations) {
		filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, operation, 0, 1));
		filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EXDEV));
	}
	filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));
	const sock_fprog program = { static_cast<unsigned short>(filter.size()), filter.data() };
	if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program) != 0)
		return false;
	const bool profileRejected = !config.Flush();
	const bool backupRejected = !FolderAccess::BackupConfig(path);
	wxString originalContents, backupContents;
	wxFile original(path), saved(backup);
	return profileRejected && backupRejected && original.ReadAll(&originalContents) &&
	       saved.ReadAll(&backupContents) && originalContents == "old=1\n" && backupContents == "old=1\n";
}

int main(int argc, char **argv)
{
	if (argc != 4 || !wxEntryStart(argc, argv)) {
		return 77;
	}
	wxTheApp->CallOnInit();
	delete wxLog::SetActiveTarget(new wxLogStderr());
	CPreferences::SetConfigDir(wxString::FromUTF8(argv[1]) + "/");
	const std::string mode(argv[3]);
	if (mode == "signature-enabled" || mode == "signature-disabled") {
		CPreferences::SetOnlineSignatureEnabled(mode == "signature-enabled");
		CPreferences::SetOSDir(CPath(wxString::FromUTF8(argv[2])));
	}
	wxString sandboxError;
	if (!FolderAccess::RestrictWrites(sandboxError)) {
		std::fprintf(stderr, "%s\n", sandboxError.utf8_str().data());
		wxEntryCleanup();
		return 77;
	}
	if (mode == "signature-enabled" || mode == "signature-disabled") {
		const wxString path = wxString::FromUTF8(argv[2]) + "/onlinesig.dat";
		const int fd = open(path.fn_str(), O_CREAT | O_WRONLY, 0600);
		const bool ok = mode == "signature-enabled" ? fd >= 0 : fd < 0 && errno == EACCES;
		if (fd >= 0)
			close(fd);
		wxTheApp->OnExit();
		wxEntryCleanup();
		return ok ? 0 : 1;
	}
	if (std::string(argv[3]) == "renamefail") {
		const bool ok = CheckRejectedPromotion(wxString::FromUTF8(argv[1]));
		wxTheApp->OnExit();
		wxEntryCleanup();
		return ok ? 0 : 1;
	}
	if (std::string(argv[3]) == "atomic") {
		const bool ok = CheckAtomicSave(wxString::FromUTF8(argv[1]));
		wxTheApp->OnExit();
		wxEntryCleanup();
		return ok ? 0 : 1;
	}
	if (std::string(argv[3]) == "text") {
		const bool ok = CheckTextSave(wxString::FromUTF8(argv[1]));
		wxTheApp->OnExit();
		wxEntryCleanup();
		return ok ? 0 : 1;
	}
	if (std::string(argv[3]) == "registry") {
		const wxString base = wxString::FromUTF8(argv[2]);
		const wxString document = base + "/readonly/budget.xlsx";
		int probe = open(document.fn_str(), O_RDONLY);
		bool ok = probe >= 0;
		if (probe >= 0) {
			close(probe);
		}
		probe = open(document.fn_str(), O_WRONLY);
		ok = (probe < 0 && errno == EACCES) && ok;
		if (probe >= 0) {
			close(probe);
		}
		probe = open((base + "/downloads/download.part").fn_str(), O_CREAT | O_WRONLY, 0600);
		ok = probe >= 0 && ok;
		if (probe >= 0) {
			ok = write(probe, "partial download", 16) == 16 && ok;
			close(probe);
		}
		wxTheApp->OnExit();
		wxEntryCleanup();
		return ok ? 0 : 1;
	}
	const wxString result = FolderAccess::Choose(
		nullptr, "Approve test folder", wxString::FromUTF8(argv[2]), std::string(argv[3]) == "write");
	bool permissionMatches = true;
	if (!result.empty()) {
		const int probe = open((result + "/write-probe").fn_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
		permissionMatches =
			std::string(argv[3]) == "write" ? probe >= 0 : probe < 0 && errno == EACCES;
		if (probe >= 0) {
			close(probe);
		}
	}
	std::printf("SELECTED=%s\n", result.utf8_str().data());
	if (!result.empty()) {
		std::printf("HOST=%s\n", FolderAccess::HostPath(result).utf8_str().data());
	}
	wxTheApp->OnExit();
	wxEntryCleanup();
	return permissionMatches ? 0 : 1;
}
