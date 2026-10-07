// Tests the production disk parser and main-thread adoption without a running app.
#include <muleunit/test.h>
#include <kademlia/kademlia/Indexed.h>
#include <kademlia/kademlia/Kademlia.h>
#include <kademlia/net/KademliaUDPListener.h>
#include <CFile.h>
#include <Preferences.h>
#include <wx/filename.h>
#include <wx/file.h>
#include <thread>
#include <chrono>
#include <tags/FileTags.h>

using namespace muleunit;
using namespace Kademlia;
DECLARE_SIMPLE(IndexedLoad)

// The standalone index never sends a packet or calls the default app constructor.
CKademlia *CKademlia::instance = nullptr;
CPacketTracking::~CPacketTracking() = default;
CKademliaUDPListener::~CKademliaUDPListener() = default;
void CKademliaUDPListener::ProcessPacket(
	const uint8_t *, uint32_t, uint32_t, uint16_t, bool, const CKadUDPKey &)
{
	throw std::runtime_error("unexpected packet");
}
wxString CPreferences::s_configDir;
void CKademliaUDPListener::SendPacket(
	const CMemFile &, uint8_t, uint32_t, uint16_t, const CKadUDPKey &, const CUInt128 *)
{
	throw std::runtime_error("unexpected packet");
}

class TempIndex
{
public:
	wxString path;
	TempIndex()
	{
		path = wxFileName::CreateTempFileName("amule-kad-index-");
		wxRemoveFile(path);
		wxMkdir(path);
		path += wxFileName::GetPathSeparator();
	}
	~TempIndex() { wxFileName::Rmdir(path, wxPATH_RMDIR_RECURSIVE); }
};
static bool WaitForLoad(CIndexed &index)
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while (index.GetLoadState() == CIndexed::LoadState::Loading &&
		std::chrono::steady_clock::now() < deadline) {
		index.ProcessIndexLoad();
		std::this_thread::yield();
	}
	return index.IsReady();
}
class DiskEntry : public CKeyEntry
{
public:
	DiskEntry()
	: CKeyEntry(std::make_shared<PublishTracking>())
	{
		m_uSize = 123;
		SetFileName("fixture.bin");
		AddTag(new CTagString(TAG_FILETYPE, "Program"), 0);
		m_publishingIPs = new PublishingIPList;
		m_publishingIPs->push_back({ 0x01020304, time(nullptr), CKadAICHHashList::INVALID_INDEX });
		AdjustGlobalPublishTracking(0x01020304, true, "fixture");
	}
};
TEST(IndexedLoad, KeywordVersionsThreeAndFourRoundTrip)
{
	const bool old = CPreferences::GetKadProtocol10();
	for (uint32_t version : { 3u, 4u }) {
		TempIndex temp;
		CPreferences::SetKadProtocol10(version == 4);
		{
			CFile file(temp.path + "key_index.dat", CFile::write);
			file.WriteUInt32(version);
			file.WriteUInt32(time(nullptr) + 3600);
			file.WriteUInt128(CUInt128(1u));
			file.WriteUInt32(1);
			file.WriteUInt128(CUInt128(2u));
			file.WriteUInt32(1);
			file.WriteUInt128(CUInt128(3u));
			file.WriteUInt32(1);
			file.WriteUInt32(time(nullptr) + 3600);
			DiskEntry entry;
			entry.WritePublishTrackingDataToFile(&file, version == 4);
			entry.WriteTagList(&file);
		}
		{
			CIndexed index(temp.path, CUInt128(1u));
			ASSERT_FALSE(index.IsReady());
			ASSERT_EQUALS(0u, index.m_totalIndexKeyword);
			ASSERT_TRUE(WaitForLoad(index));
			ASSERT_EQUALS(1u, index.m_totalIndexKeyword);
			ASSERT_EQUALS(size_t(1), index.GetFileKeyCount());
		}
		{
			CIndexed reloaded(temp.path, CUInt128(1u));
			ASSERT_TRUE(WaitForLoad(reloaded));
			ASSERT_EQUALS(1u, reloaded.m_totalIndexKeyword);
		}
	}
	CPreferences::SetKadProtocol10(old);
}
TEST(IndexedLoad, SourcesAndLoadEntriesAreAdopted)
{
	TempIndex temp;
	{
		CFile file(temp.path + "src_index.dat", CFile::write);
		file.WriteUInt32(2);
		file.WriteUInt32(time(nullptr) + 3600);
		file.WriteUInt32(1);
		file.WriteUInt128(CUInt128(2u));
		file.WriteUInt32(1);
		file.WriteUInt128(CUInt128(3u));
		file.WriteUInt32(1);
		file.WriteUInt32(time(nullptr) + 3600);
		file.WriteUInt8(3);
		file.WriteTag(CTagVarInt(TAG_SOURCEIP, 0x01020304));
		file.WriteTag(CTagVarInt(TAG_SOURCEPORT, 4662));
		file.WriteTag(CTagVarInt(TAG_SOURCEUPORT, 4672));
	}
	{
		CFile file(temp.path + "load_index.dat", CFile::write);
		file.WriteUInt32(1);
		file.WriteUInt32(time(nullptr));
		file.WriteUInt32(1);
		file.WriteUInt128(CUInt128(2u));
		file.WriteUInt32(time(nullptr) + 3600);
	}
	CIndexed index(temp.path, CUInt128(1u));
	ASSERT_TRUE(WaitForLoad(index));
	ASSERT_EQUALS(1u, index.m_totalIndexSource);
	ASSERT_EQUALS(1u, index.m_totalIndexLoad);
}
TEST(IndexedLoad, TruncatedIndexIsPreservedAfterFailure)
{
	TempIndex temp;
	{
		CFile file(temp.path + "key_index.dat", CFile::write);
		file.WriteUInt32(4);
	}
	{
		CIndexed index(temp.path, CUInt128(1u));
		ASSERT_FALSE(WaitForLoad(index));
		ASSERT_TRUE(index.GetLoadState() == CIndexed::LoadState::Failed);
		ASSERT_FALSE(index.AddLoad(CUInt128(2u), time(nullptr) + 3600));
		ASSERT_EQUALS(0u, index.m_totalIndexKeyword);
	}
	CFile file(temp.path + "key_index.dat");
	ASSERT_EQUALS(uint64_t(4), file.GetLength());
	ASSERT_EQUALS(4u, file.ReadUInt32());
}
TEST(IndexedLoad, StopBeforeAdoptionDoesNotRewriteFiles)
{
	TempIndex temp;
	{
		CFile file(temp.path + "load_index.dat", CFile::write);
		file.WriteUInt32(1);
		file.WriteUInt32(123);
		file.WriteUInt32(0);
	}
	{
		CIndexed index(temp.path, CUInt128(1u));
	}
	CFile file(temp.path + "load_index.dat");
	ASSERT_EQUALS(1u, file.ReadUInt32());
	ASSERT_EQUALS(123u, file.ReadUInt32());
	ASSERT_FALSE(wxFileExists(temp.path + "key_index.dat"));
}
TEST(IndexedLoad, WrongIdentitySkipsKeywordIndex)
{
	TempIndex temp;
	{
		CFile file(temp.path + "key_index.dat", CFile::write);
		file.WriteUInt32(3);
		file.WriteUInt32(time(nullptr) + 3600);
		file.WriteUInt128(CUInt128(99u));
		file.WriteUInt32(0);
	}
	CIndexed index(temp.path, CUInt128(1u));
	ASSERT_TRUE(WaitForLoad(index));
	ASSERT_EQUALS(0u, index.m_totalIndexKeyword);
}

TEST(IndexedLoad, UnsupportedVersionIsPreserved)
{
	TempIndex temp;
	{
		CFile file(temp.path + "src_index.dat", CFile::write);
		file.WriteUInt32(99);
	}
	{
		CIndexed index(temp.path, CUInt128(1u));
		ASSERT_FALSE(WaitForLoad(index));
		ASSERT_TRUE(index.GetLoadState() == CIndexed::LoadState::Failed);
	}
	CFile file(temp.path + "src_index.dat");
	ASSERT_EQUALS(uint64_t(4), file.GetLength());
	ASSERT_EQUALS(99u, file.ReadUInt32());
}

TEST(IndexedLoad, PartiallyDecodedKeywordIsDiscarded)
{
	TempIndex temp;
	uint64_t truncatedSize;
	{
		CFile file(temp.path + "key_index.dat", CFile::write);
		file.WriteUInt32(4);
		file.WriteUInt32(time(nullptr) + 3600);
		file.WriteUInt128(CUInt128(1u));
		file.WriteUInt32(1);
		file.WriteUInt128(CUInt128(2u));
		file.WriteUInt32(1);
		file.WriteUInt128(CUInt128(3u));
		file.WriteUInt32(1);
		file.WriteUInt32(time(nullptr) + 3600);
		DiskEntry entry;
		entry.WritePublishTrackingDataToFile(&file, true);
		file.WriteUInt8(1); // incomplete string tag, after publisher data was loaded
		truncatedSize = file.GetLength();
	}
	{
		CIndexed index(temp.path, CUInt128(1u));
		ASSERT_FALSE(WaitForLoad(index));
		ASSERT_EQUALS(0u, index.m_totalIndexKeyword);
	}
	CFile file(temp.path + "key_index.dat");
	ASSERT_EQUALS(truncatedSize, file.GetLength());
}

TEST(IndexedLoad, WrongTypeSourceTagIsDiscarded)
{
	TempIndex temp;
	uint64_t originalSize;
	{
		CFile file(temp.path + "src_index.dat", CFile::write);
		file.WriteUInt32(2);
		file.WriteUInt32(time(nullptr) + 3600);
		file.WriteUInt32(1);
		file.WriteUInt128(CUInt128(2u));
		file.WriteUInt32(1);
		file.WriteUInt128(CUInt128(3u));
		file.WriteUInt32(1);
		file.WriteUInt32(time(nullptr) + 3600);
		file.WriteUInt8(1);
		file.WriteTag(CTagString(TAG_SOURCEIP, "invalid integer"));
		originalSize = file.GetLength();
	}
	{
		CIndexed index(temp.path, CUInt128(1u));
		ASSERT_FALSE(WaitForLoad(index));
		ASSERT_TRUE(index.GetLoadState() == CIndexed::LoadState::Failed);
		ASSERT_EQUALS(0u, index.m_totalIndexSource);
	}
	CFile file(temp.path + "src_index.dat");
	ASSERT_EQUALS(originalSize, file.GetLength());
}

TEST(IndexedLoad, LargeLoadIndexAdoptsAndRoundTripsAsOneResult)
{
	TempIndex temp;
	constexpr uint32_t count = 50000;
	{
		CFile file(temp.path + "load_index.dat", CFile::write);
		file.WriteUInt32(1);
		file.WriteUInt32(time(nullptr));
		file.WriteUInt32(count);
		for (uint32_t i = 1; i <= count; ++i) {
			file.WriteUInt128(CUInt128(i));
			file.WriteUInt32(time(nullptr) + 3600);
		}
	}
	{
		CIndexed index(temp.path, CUInt128(1u));
		ASSERT_EQUALS(0u, index.m_totalIndexLoad);
		ASSERT_TRUE(WaitForLoad(index));
		ASSERT_EQUALS(count, index.m_totalIndexLoad);
	}
	CIndexed reloaded(temp.path, CUInt128(1u));
	ASSERT_TRUE(WaitForLoad(reloaded));
	ASSERT_EQUALS(count, reloaded.m_totalIndexLoad);
}

TEST(IndexedLoad, PublisherDecoderHonorsCancellation)
{
	TempIndex temp;
	const wxString path = temp.path + "publisher.dat";
	{
		CFile file(path, CFile::write);
		DiskEntry entry;
		entry.WritePublishTrackingDataToFile(&file, true);
	}
	CFile file(path);
	const auto position = file.GetPosition();
	std::atomic<bool> cancel{ true };
	CKeyEntry entry(std::make_shared<CKeyEntry::PublishTracking>());
	bool cancelled = false;
	try {
		entry.ReadPublishTrackingDataFromFile(&file, true, &cancel);
	} catch (const std::runtime_error &) {
		cancelled = true;
	}
	ASSERT_TRUE(cancelled);
	ASSERT_EQUALS(position, file.GetPosition());
}

class CancellingPublisherFile : public CFile
{
	std::atomic<bool> &m_cancel;

public:
	CancellingPublisherFile(const wxString &path, std::atomic<bool> &cancel)
	: CFile(path)
	, m_cancel(cancel)
	{
	}
	void Read(void *buffer, size_t count) const override
	{
		CFile::Read(buffer, count);
		if (GetPosition() >= 20) {
			m_cancel.store(true);
		}
	}
};
TEST(IndexedLoad, CancellationStopsInsidePublisherListAndReleasesTracking)
{
	TempIndex temp;
	const wxString path = temp.path + "publishers.dat";
	{
		CFile file(path, CFile::write);
		file.WriteUInt16(0); // no AICH hashes
		file.WriteUInt32(0); // no names
		file.WriteUInt32(10000);
		for (uint32_t i = 0; i < 10000; ++i) {
			file.WriteUInt32(0x01020304 + i);
			file.WriteUInt32(time(nullptr));
			file.WriteUInt16(CKadAICHHashList::INVALID_INDEX);
		}
	}
	std::atomic<bool> cancel{ false };
	CancellingPublisherFile file(path, cancel);
	auto tracking = std::make_shared<CKeyEntry::PublishTracking>();
	{
		CKeyEntry entry(tracking);
		bool stopped = false;
		try {
			entry.ReadPublishTrackingDataFromFile(&file, true, &cancel);
		} catch (const std::runtime_error &) {
			stopped = true;
		}
		ASSERT_TRUE(stopped);
		ASSERT_EQUALS(uint64_t(20), file.GetPosition());
		ASSERT_EQUALS(size_t(1), tracking->size());
	}
	ASSERT_TRUE(tracking->empty());
}
