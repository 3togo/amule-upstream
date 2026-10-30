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

#ifndef AMULE_FOLDERACCESS_H
#define AMULE_FOLDERACCESS_H

#include <vector>
#include <common/Path.h>

class wxWindow;
class wxFileConfig;
class wxMBConv;

namespace FolderAccess
{
bool IsSandboxed();
// Applied before loading downloads or shared content. Failure stops startup.
bool RestrictWrites(wxString &error);
// Atomic owner-only profile writes which need no metadata mutation after startup.
bool SaveConfig(wxFileConfig &config, const wxString &path, const wxMBConv &conversion);
bool BackupConfig(const wxString &path);
// Original host location of a portal path, retained for safe reauthorization.
wxString HostPath(const wxString &path);
// Approved roots remain available independently of whether they are shared.
std::vector<CPath> ApprovedFolders();
#ifndef AMULE_DAEMON
// In Flatpak, always use the host portal. A typed path is only a chooser hint;
// it never bypasses the system's approval. Empty result means cancelled or failed.
wxString Choose(wxWindow *parent, const wxString &title, const wxString &hint, bool writable);
#endif
} // namespace FolderAccess
#endif
