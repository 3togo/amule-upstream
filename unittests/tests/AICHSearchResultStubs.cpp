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
