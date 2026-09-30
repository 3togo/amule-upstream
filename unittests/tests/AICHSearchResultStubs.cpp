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

// Application dependencies of the production SHAHashSet.cpp.
// Consensus tests must never enter client recovery; fail at that boundary.
#include <muleunit/test.h>
#include <amule.h>
#include <Preferences.h>
#include <DownloadQueue.h>
#include <PartFile.h>
#include <updownclient.h>

using namespace muleunit;

CamuleDaemonApp *theApp = nullptr;
bool CPreferences::s_AICHTrustEveryHash = false;
wxString CPreferences::s_configDir;

void CClientRef::Unlink()
{
	ASSERT_TRUE(m_client == nullptr);
}

CClientRef::CClientRef(const CClientRef &other)
: m_client(nullptr)
{
	ASSERT_TRUE(other.m_client == nullptr);
}

void CUpDownClient::SetReqFileAICHHash(CAICHHash *)
{
	FAIL_M("AICH consensus test unexpectedly entered client recovery");
}

bool CDownloadQueue::IsPartFile(const CKnownFile *) const
{
	FAIL_M("AICH consensus test unexpectedly queried the download queue");
	return false;
}

void CPartFile::RequestAICHRecovery(uint16)
{
	FAIL_M("AICH consensus test unexpectedly requested recovery");
}

#ifdef __DEBUG__
wxString CUpDownClient::GetClientFullInfo()
{
	FAIL_M("AICH consensus test unexpectedly inspected a recovery client");
	return wxString();
}

#endif
