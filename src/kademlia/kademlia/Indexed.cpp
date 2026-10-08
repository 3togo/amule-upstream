//
// This file is part of the aMule Project.
//
// Copyright (c) 2004-2011 Angel Vidal ( kry@amule.org )
// Copyright (c) 2003-2026 aMule Team ( https://amule-org.github.io )
// Copyright (c) 2003-2011 Barry Dunne (http://www.emule-project.net)
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

// Note To Mods //
/*
Please do not change anything here and release it..
There is going to be a new forum created just for the Kademlia side of the client..
If you feel there is an error or a way to improve something, please
post it in the forum first and let us look at it.. If it is a real improvement,
it will be added to the official client.. Changing something without knowing
what all it does can cause great harm to the network if released in mass form..
Any mod that changes anything within the Kademlia side will not be allowed to advertise
there client on the eMule forum..
*/

#include "Indexed.h"
#include <stdexcept>
#include <algorithm>

#include <protocol/Protocols.h>
#include <protocol/ed2k/Constants.h>
#include <protocol/kad/Constants.h>
#include <protocol/kad/Client2Client/UDP.h>
#include <protocol/kad2/Client2Client/UDP.h>
#include <common/Macros.h>
#include <tags/FileTags.h>

#include "../routing/Contact.h"
#include "../net/KademliaUDPListener.h"
#include "../utils/KadUDPKey.h"
#include "../../CFile.h"
#include "../../MemFile.h"
#include "../../Preferences.h"
#include "../../Logger.h"

////////////////////////////////////////
using namespace Kademlia;
////////////////////////////////////////

CIndexed::CIndexed()
: CIndexed(thePrefs::GetConfigDir(), CKademlia::GetPrefs()->GetKadID())
{
}

CIndexed::CIndexed(const wxString &directory, const CUInt128 &kadID)
: CIndexed(directory, kadID, false)
{
	m_loader = std::make_unique<CBackgroundLoad<std::unique_ptr<CIndexed>>>(
		[directory, kadID](const std::atomic<bool> &cancel) {
			auto loaded = std::unique_ptr<CIndexed>(new CIndexed(directory, kadID, true));
			loaded->ReadFile(cancel);
			return loaded;
		});
}

CIndexed::CIndexed(const wxString &directory, const CUInt128 &kadID, bool worker)
: m_publishTracking(std::make_shared<CKeyEntry::PublishTracking>())
, m_loadState(worker ? LoadState::Ready : LoadState::Loading)
, m_worker(worker)
, m_kadID(kadID)
{
	m_sfilename = directory + "src_index.dat";
	m_kfilename = directory + "key_index.dat";
	m_loadfilename = directory + "load_index.dat";
	m_lastClean = time(NULL) + (60 * 30);
	m_totalIndexSource = m_totalIndexKeyword = m_totalIndexNotes = m_totalIndexLoad = 0;
}

void CIndexed::ProcessIndexLoad()
{
	if (!m_loader || m_loadState != LoadState::Loading) {
		return;
	}
	std::unique_ptr<CIndexed> loaded;
	if (m_loader->Take(loaded)) {
		for (const auto &path : loaded->m_failedFiles) {
			if (!CPath::FileExists(path)) {
				continue;
			}
			if (CPath::RenameFile(CPath(path), CPath(path + ".bad"), true)) {
				AddLogLineC(CFormat(_("Kad could not read %s. It was renamed to %s.bad, and "
						      "a new index will be saved at shutdown.")) %
					    path % path);
			} else {
				m_preservedFiles.push_back(path);
				AddLogLineC(CFormat(_("Kad could not preserve the unreadable index %s as "
						      "%s.bad. Its original file will not be overwritten.")) %
					    path % path);
			}
		}
		m_Keyword_map.swap(loaded->m_Keyword_map);
		m_Sources_map.swap(loaded->m_Sources_map);
		m_Load_map.swap(loaded->m_Load_map);
		m_publishTracking.swap(loaded->m_publishTracking);
		m_totalIndexKeyword = loaded->m_totalIndexKeyword;
		m_totalIndexSource = loaded->m_totalIndexSource;
		m_totalIndexLoad = loaded->m_totalIndexLoad;
		m_loadState = LoadState::Ready;
		AddDebugLogLineN(logKadIndex, "Kad indexes ready");
	} else if (m_loader->GetState() == LoadState::Failed) {
		m_loadState = LoadState::Ready;
		m_preservedFiles = { m_loadfilename, m_kfilename, m_sfilename };
		AddLogLineC(_("Kad could not load its saved index. Existing index files were preserved."));
		AddDebugLogLineC(logKadIndex,
			"Kad index load failed; persisted files will be preserved: " +
				wxString::FromUTF8(m_loader->Error()));
	}
	if (m_loadState != LoadState::Loading) {
		m_loader.reset();
	}
}

namespace
{
bool OpenIndex(CFile &file, const wxString &path)
{
	if (!CPath::FileExists(path)) {
		return false;
	}
	if (!file.Open(path, CFile::read)) {
		throw CIOFailureException("Cannot open persisted Kad index");
	}
	return true;
}
} // namespace

void CIndexed::ReadFile(const std::atomic<bool> &cancel)
{
	try {
		for (const auto part : { IndexPart::Load, IndexPart::Keyword, IndexPart::Source }) {
			if (cancel.load()) {
				return;
			}
			try {
				switch (part) {
				case IndexPart::Load:
					ReadLoadFile(cancel);
					break;
				case IndexPart::Keyword:
					ReadKeywordFile(cancel);
					break;
				case IndexPart::Source:
					ReadSourceFile(cancel);
					break;
				}
				continue;
			} catch (const CSafeIOException &) {
			} catch (const CInvalidPacket &) {
			} catch (const std::runtime_error &) {
			} catch (const wxString &) {
			}
			// The publisher decoder also throws when cancellation is requested.
			if (cancel.load()) {
				return;
			}
			ClearPart(part);
			m_failedFiles.push_back(PathOf(part));
		}
	} catch (...) {
		if (cancel.load()) {
			return;
		}
		// Unexpected errors discard all partial data, but must not disable Kad.
		Clear();
		m_failedFiles = { m_loadfilename, m_kfilename, m_sfilename };
	}
}

void CIndexed::ReadLoadFile(const std::atomic<bool> &cancel)
{
	CFile load_file;
	if (OpenIndex(load_file, m_loadfilename)) {
		uint32_t version = load_file.ReadUInt32();
		if (version >= 2) {
			throw std::runtime_error("unsupported persisted Kad index version");
		}
		if (version < 2) {
			/*time_t savetime =*/load_file.ReadUInt32(); //  Savetime is unused now

			uint32_t numLoad = load_file.ReadUInt32();
			while (numLoad) {
				if (cancel.load()) {
					return;
				}
				CUInt128 keyID = load_file.ReadUInt128();
				AddLoad(keyID, load_file.ReadUInt32());
				numLoad--;
			}
		}
		load_file.Close();
	}
}
void CIndexed::ReadKeywordFile(const std::atomic<bool> &cancel)
{
	CFile k_file;
	if (OpenIndex(k_file, m_kfilename)) {
		uint32_t version = k_file.ReadUInt32();
		if (version >= 5) {
			throw std::runtime_error("unsupported persisted Kad index version");
		}
		// Version 4 added the AICH hash block and the per-publisher hash index that
		// Kad protocol version 0x09 keyword storage needs; version 3 files still
		// load, just without any AICH hash.
		if (version < 5) {
			time_t savetime = k_file.ReadUInt32();
			if (savetime > time(NULL)) {
				CUInt128 id = k_file.ReadUInt128();
				if (m_kadID == id) {
					uint32_t numKeys = k_file.ReadUInt32();
					while (numKeys) {
						if (cancel.load()) {
							return;
						}
						CUInt128 keyID = k_file.ReadUInt128();
						uint32_t numSource = k_file.ReadUInt32();
						while (numSource) {
							if (cancel.load()) {
								return;
							}
							CUInt128 sourceID = k_file.ReadUInt128();
							uint32_t numName = k_file.ReadUInt32();
							while (numName) {
								if (cancel.load()) {
									return;
								}
								auto ownedEntry = std::unique_ptr<CKeyEntry>(
									CreateKeyEntry());
								CKeyEntry *toAdd = ownedEntry.get();
								toAdd->m_uKeyID = keyID;
								toAdd->m_uSourceID = sourceID;
								toAdd->m_bSource = false;
								toAdd->m_tLifeTime = k_file.ReadUInt32();
								if (version >= 3) {
									toAdd->ReadPublishTrackingDataFromFile(
										&k_file,
										version >= 4,
										&cancel);
								}
								uint32_t tagList = k_file.ReadUInt8();
								while (tagList) {
									auto ownedTag = std::unique_ptr<CTag>(
										k_file.ReadTag());
									CTag *tag = ownedTag.get();
									if (tag) {
										if (!tag->GetName().Cmp(
											    TAG_FILENAME)) {
											if (toAdd->GetCommonFileName()
													.IsEmpty()) {
												toAdd->SetFileName(
													tag->GetStr());
											}
										} else if (
											!tag->GetName().Cmp(
												TAG_FILESIZE)) {
											if (tag->IsBsob() &&
												(tag->GetBsobSize() ==
													8)) {
												// Older
												// builds
												// wrongly
												// saved
												// BSOB
												// uint64s
												// to
												// key_index.dat,
												// so
												// those
												// have
												// to
												// be
												// handled
												// here
												// too.
												toAdd->m_uSize = PeekUInt64(
													tag->GetBsob());
											} else {
												toAdd->m_uSize =
													tag->GetInt();
											}
										} else if (
											!tag->GetName().Cmp(
												TAG_SOURCEIP)) {
											toAdd->m_uIP =
												tag->GetInt();
											toAdd->AddTag(tag, 0);
											ownedTag.release();
										} else if (
											!tag->GetName().Cmp(
												TAG_SOURCEPORT)) {
											toAdd->m_uTCPport =
												tag->GetInt();
											toAdd->AddTag(tag, 0);
											ownedTag.release();
										} else if (
											!tag->GetName().Cmp(
												TAG_SOURCEUPORT)) {
											toAdd->m_uUDPport =
												tag->GetInt();
											toAdd->AddTag(tag, 0);
											ownedTag.release();
										} else {
											toAdd->AddTag(tag, 0);
											ownedTag.release();
										}
									}
									tagList--;
								}
								uint8_t load;
								if (AddKeyword(
									    keyID, sourceID, toAdd, load)) {
									ownedEntry.release();
								}
								numName--;
							}
							numSource--;
						}
						numKeys--;
					}
				}
			}
		}
		k_file.Close();
	}
}
void CIndexed::ReadSourceFile(const std::atomic<bool> &cancel)
{
	CFile s_file;
	if (OpenIndex(s_file, m_sfilename)) {
		uint32_t version = s_file.ReadUInt32();
		if (version >= 3) {
			throw std::runtime_error("unsupported persisted Kad index version");
		}
		if (version < 3) {
			time_t savetime = s_file.ReadUInt32();
			if (savetime > time(NULL)) {
				uint32_t numKeys = s_file.ReadUInt32();
				while (numKeys) {
					if (cancel.load()) {
						return;
					}
					CUInt128 keyID = s_file.ReadUInt128();
					uint32_t numSource = s_file.ReadUInt32();
					while (numSource) {
						if (cancel.load()) {
							return;
						}
						CUInt128 sourceID = s_file.ReadUInt128();
						uint32_t numName = s_file.ReadUInt32();
						while (numName) {
							if (cancel.load()) {
								return;
							}
							auto ownedEntry = std::make_unique<CEntry>();
							CEntry *toAdd = ownedEntry.get();
							toAdd->m_bSource = true;
							toAdd->m_tLifeTime = s_file.ReadUInt32();
							uint32_t tagList = s_file.ReadUInt8();
							while (tagList) {
								auto ownedTag = std::unique_ptr<CTag>(
									s_file.ReadTag());
								CTag *tag = ownedTag.get();
								if (tag) {
									if (!tag->GetName().Cmp(
										    TAG_SOURCEIP)) {
										toAdd->m_uIP = tag->GetInt();
										toAdd->AddTag(tag, 0);
										ownedTag.release();
									} else if (!tag->GetName().Cmp(
											   TAG_SOURCEPORT)) {
										toAdd->m_uTCPport =
											tag->GetInt();
										toAdd->AddTag(tag, 0);
										ownedTag.release();
									} else if (!tag->GetName().Cmp(
											   TAG_SOURCEUPORT)) {
										toAdd->m_uUDPport =
											tag->GetInt();
										toAdd->AddTag(tag, 0);
										ownedTag.release();
									} else {
										toAdd->AddTag(tag, 0);
										ownedTag.release();
									}
								}
								tagList--;
							}
							toAdd->m_uKeyID = keyID;
							toAdd->m_uSourceID = sourceID;
							uint8_t load;
							if (AddSources(keyID, sourceID, toAdd, load)) {
								ownedEntry.release();
							}
							numName--;
						}
						numSource--;
					}
					numKeys--;
				}
			}
		}
		s_file.Close();
	}
}

CIndexed::~CIndexed()
{
	// Joining happens before any worker captures or loaded values are released.
	m_loader.reset();
	if (!m_worker && IsReady()) {
		WriteFile();
	}
	Clear();
}

void CIndexed::SaveIndexFile(const wxString &path, const std::function<void(CFile &)> &write)
{
	if (std::find(m_preservedFiles.begin(), m_preservedFiles.end(), path) != m_preservedFiles.end()) {
		return;
	}
	try {
		CFile file;
		if (!file.Open(path, CFile::write_safe)) {
			throw CIOFailureException("Cannot open saved Kad index staging file");
		}
		write(file);
		if (!file.Flush() || !file.Close()) {
			throw CIOFailureException("Cannot commit saved Kad index");
		}
		return;
	} catch (const CSafeIOException &err) {
		AddDebugLogLineC(logKadIndex, "CIndexed::SaveIndexFile: " + err.what());
	} catch (const CInvalidPacket &err) {
		AddDebugLogLineC(logKadIndex, "CIndexed::SaveIndexFile: " + err.what());
	} catch (const wxString &err) {
		AddDebugLogLineC(logKadIndex, "CIndexed::SaveIndexFile: " + err);
	}
	// The file destructor has closed the staging file without replacing the original.
	if (CPath::FileExists(path + ".new")) {
		CPath::RemoveFile(CPath(path + ".new"));
	}
	AddLogLineC(_("Kad could not save an index. The previous copy of that file was preserved."));
}

void CIndexed::WriteFile()
{
	time_t now = time(NULL);
	uint32_t s_total = 0;
	uint32_t k_total = 0;
	uint32_t l_total = 0;
	SaveIndexFile(m_loadfilename, [&](CFile &load_file) {
		load_file.WriteUInt32(1); // version
		load_file.WriteUInt32(now);
		wxASSERT(m_Load_map.size() < 0xFFFFFFFF);
		load_file.WriteUInt32((uint32_t)m_Load_map.size());
		for (LoadMap::iterator it = m_Load_map.begin(); it != m_Load_map.end(); ++it) {
			Load *load = it->second;
			wxASSERT(load);
			if (load) {
				load_file.WriteUInt128(load->keyID);
				load_file.WriteUInt32(load->time);
				l_total++;
			}
		}
	});

	SaveIndexFile(m_sfilename, [&](CFile &s_file) {
		s_file.WriteUInt32(2); // version
		s_file.WriteUInt32(now + KADEMLIAREPUBLISHTIMES);
		wxASSERT(m_Sources_map.size() < 0xFFFFFFFF);
		s_file.WriteUInt32((uint32_t)m_Sources_map.size());
		for (SrcHashMap::iterator itSrcHash = m_Sources_map.begin(); itSrcHash != m_Sources_map.end();
			++itSrcHash) {
			SrcHash *currSrcHash = itSrcHash->second;
			s_file.WriteUInt128(currSrcHash->keyID);

			CKadSourcePtrList &KeyHashSrcMap = currSrcHash->m_Source_map;
			wxASSERT(KeyHashSrcMap.size() < 0xFFFFFFFF);
			s_file.WriteUInt32((uint32_t)KeyHashSrcMap.size());

			for (CKadSourcePtrList::iterator itSource = KeyHashSrcMap.begin();
				itSource != KeyHashSrcMap.end();
				++itSource) {
				Source *currSource = *itSource;
				s_file.WriteUInt128(currSource->sourceID);

				CKadEntryPtrList &SrcEntryList = currSource->entryList;
				wxASSERT(SrcEntryList.size() < 0xFFFFFFFF);
				s_file.WriteUInt32((uint32_t)SrcEntryList.size());
				for (CKadEntryPtrList::iterator itEntry = SrcEntryList.begin();
					itEntry != SrcEntryList.end();
					++itEntry) {
					Kademlia::CEntry *currName = *itEntry;
					s_file.WriteUInt32(currName->m_tLifeTime);
					currName->WriteTagList(&s_file);

					s_total++;
				}
			}
		}
	});

	SaveIndexFile(m_kfilename, [&](CFile &k_file) {
		// Version 4 carries the AICH block and the per-publisher hash index; gated
		// with the writer in CKeyEntry::WritePublishTrackingDataToFile, so with
		// KadProtocol10 off we write the version-3 file upstream writes. Reading
		// both is unconditional, so toggling the preference never invalidates an
		// existing keyword index.
		// Saving with the preference off discards accumulated AICH data;
		// re-enabling it starts collecting that data again from empty.
		const bool includesAICH = thePrefs::GetKadProtocol10();
		k_file.WriteUInt32(includesAICH ? 4 : 3);
		k_file.WriteUInt32(now + KADEMLIAREPUBLISHTIMEK);
		k_file.WriteUInt128(m_kadID);

		wxASSERT(m_Keyword_map.size() < 0xFFFFFFFF);
		k_file.WriteUInt32((uint32_t)m_Keyword_map.size());

		for (KeyHashMap::iterator itKeyHash = m_Keyword_map.begin(); itKeyHash != m_Keyword_map.end();
			++itKeyHash) {
			KeyHash *currKeyHash = itKeyHash->second;
			k_file.WriteUInt128(currKeyHash->keyID);

			CSourceKeyMap &KeyHashSrcMap = currKeyHash->m_Source_map;
			wxASSERT(KeyHashSrcMap.size() < 0xFFFFFFFF);
			k_file.WriteUInt32((uint32_t)KeyHashSrcMap.size());

			for (CSourceKeyMap::iterator itSource = KeyHashSrcMap.begin();
				itSource != KeyHashSrcMap.end();
				++itSource) {
				Source *currSource = itSource->second;
				k_file.WriteUInt128(currSource->sourceID);

				CKadEntryPtrList &SrcEntryList = currSource->entryList;
				wxASSERT(SrcEntryList.size() < 0xFFFFFFFF);
				k_file.WriteUInt32((uint32_t)SrcEntryList.size());

				for (CKadEntryPtrList::iterator itEntry = SrcEntryList.begin();
					itEntry != SrcEntryList.end();
					++itEntry) {
					Kademlia::CKeyEntry *currName =
						static_cast<Kademlia::CKeyEntry *>(*itEntry);
					wxASSERT(currName->IsKeyEntry());
					k_file.WriteUInt32(currName->m_tLifeTime);
					currName->WritePublishTrackingDataToFile(&k_file, includesAICH);
					currName->WriteTagList(&k_file);

					k_total++;
				}
			}
		}
	});
	AddDebugLogLineN(logKadIndex,
		CFormat("Wrote %u source, %u keyword, and %u load entries") % s_total % k_total % l_total);
}

const wxString &CIndexed::PathOf(IndexPart part) const
{
	switch (part) {
	case IndexPart::Load:
		return m_loadfilename;
	case IndexPart::Keyword:
		return m_kfilename;
	case IndexPart::Source:
		return m_sfilename;
	}
	throw std::logic_error("Invalid Kad index part");
}

void CIndexed::ClearPart(IndexPart part)
{
	if (part == IndexPart::Keyword) {
		for (auto &keyword : m_Keyword_map) {
			for (auto &source : keyword.second->m_Source_map) {
				for (auto *entry : source.second->entryList) {
					static_cast<CKeyEntry *>(entry)->DirtyDeletePublishData();
					delete entry;
				}
				delete source.second;
			}
			delete keyword.second;
		}
		m_Keyword_map.clear();
		m_publishTracking->clear();
		m_totalIndexKeyword = 0;
	} else if (part == IndexPart::Source) {
		for (auto &hash : m_Sources_map) {
			for (auto *source : hash.second->m_Source_map) {
				for (auto *entry : source->entryList) {
					delete entry;
				}
				delete source;
			}
			delete hash.second;
		}
		m_Sources_map.clear();
		m_totalIndexSource = 0;
	} else {
		for (auto &load : m_Load_map) {
			delete load.second;
		}
		m_Load_map.clear();
		m_totalIndexLoad = 0;
	}
}

void CIndexed::Clear()
{
	for (const auto part : { IndexPart::Load, IndexPart::Keyword, IndexPart::Source }) {
		ClearPart(part);
	}
	for (auto &hash : m_Notes_map) {
		for (auto *source : hash.second->m_Source_map) {
			for (auto *entry : source->entryList) {
				delete entry;
			}
			delete source;
		}
		delete hash.second;
	}
	m_Notes_map.clear();
	m_totalIndexNotes = 0;
}

void CIndexed::Clean()
{
	if (!IsReady()) {
		return;
	}
	time_t tNow = time(NULL);
	if (m_lastClean > tNow) {
		return;
	}

	uint32_t k_Removed = 0;
	uint32_t s_Removed = 0;
	uint32_t s_Total = 0;
	uint32_t k_Total = 0;

	KeyHashMap::iterator itKeyHash = m_Keyword_map.begin();
	while (itKeyHash != m_Keyword_map.end()) {
		KeyHashMap::iterator curr_itKeyHash = itKeyHash++; // Don't change this to a ++it!
		KeyHash *currKeyHash = curr_itKeyHash->second;

		for (CSourceKeyMap::iterator itSource = currKeyHash->m_Source_map.begin();
			itSource != currKeyHash->m_Source_map.end();) {
			CSourceKeyMap::iterator curr_itSource = itSource++; // Don't change this to a ++it!
			Source *currSource = curr_itSource->second;

			CKadEntryPtrList::iterator itEntry = currSource->entryList.begin();
			while (itEntry != currSource->entryList.end()) {
				k_Total++;

				Kademlia::CKeyEntry *currName = static_cast<Kademlia::CKeyEntry *>(*itEntry);
				wxASSERT(currName->IsKeyEntry());
				if (!currName->m_bSource && currName->m_tLifeTime < tNow) {
					k_Removed++;
					itEntry = currSource->entryList.erase(itEntry);
					delete currName;
					continue;
				} else if (currName->m_bSource) {
					wxFAIL;
				} else {
					currName->CleanUpTrackedPublishers(); // intern cleanup
				}
				++itEntry;
			}

			if (currSource->entryList.empty()) {
				currKeyHash->m_Source_map.erase(curr_itSource);
				delete currSource;
			}
		}

		if (currKeyHash->m_Source_map.empty()) {
			m_Keyword_map.erase(curr_itKeyHash);
			delete currKeyHash;
		}
	}

	SrcHashMap::iterator itSrcHash = m_Sources_map.begin();
	while (itSrcHash != m_Sources_map.end()) {
		SrcHashMap::iterator curr_itSrcHash = itSrcHash++; // Don't change this to a ++it!
		SrcHash *currSrcHash = curr_itSrcHash->second;

		CKadSourcePtrList::iterator itSource = currSrcHash->m_Source_map.begin();
		while (itSource != currSrcHash->m_Source_map.end()) {
			Source *currSource = *itSource;

			CKadEntryPtrList::iterator itEntry = currSource->entryList.begin();
			while (itEntry != currSource->entryList.end()) {
				s_Total++;

				Kademlia::CEntry *currName = *itEntry;
				if (currName->m_tLifeTime < tNow) {
					s_Removed++;
					itEntry = currSource->entryList.erase(itEntry);
					delete currName;
				} else {
					++itEntry;
				}
			}

			if (currSource->entryList.empty()) {
				itSource = currSrcHash->m_Source_map.erase(itSource);
				delete currSource;
			} else {
				++itSource;
			}
		}

		if (currSrcHash->m_Source_map.empty()) {
			m_Sources_map.erase(curr_itSrcHash);
			delete currSrcHash;
		}
	}

	m_totalIndexSource = s_Total - s_Removed;
	m_totalIndexKeyword = k_Total - k_Removed;
	AddDebugLogLineN(logKadIndex,
		CFormat("Removed %u keyword out of %u and %u source out of %u") % k_Removed % k_Total %
			s_Removed % s_Total);
	m_lastClean = tNow + MIN2S(30);
}

bool CIndexed::AddKeyword(
	const CUInt128 &keyID, const CUInt128 &sourceID, Kademlia::CKeyEntry *entry, uint8_t &load)
{
	if (!IsReady()) {
		return false;
	}
	if (!entry) {
		return false;
	}

	wxCHECK(entry->IsKeyEntry(), false);
	wxCHECK(entry->m_publishTracking == m_publishTracking, false);

	if (m_totalIndexKeyword > KADEMLIAMAXENTRIES) {
		load = 100;
		return false;
	}

	if (entry->m_uSize == 0 || entry->GetCommonFileName().IsEmpty() || entry->GetTagCount() == 0 ||
		entry->m_tLifeTime < time(NULL)) {
		return false;
	}

	KeyHashMap::iterator itKeyHash = m_Keyword_map.find(keyID);
	KeyHash *currKeyHash = NULL;
	if (itKeyHash == m_Keyword_map.end()) {
		Source *currSource = new Source;
		currSource->sourceID = sourceID;
		entry->MergeIPsAndFilenames(NULL); // IpTracking init
		currSource->entryList.push_front(entry);
		currKeyHash = new KeyHash;
		currKeyHash->keyID = keyID;
		currKeyHash->m_Source_map[currSource->sourceID] = currSource;
		m_Keyword_map[currKeyHash->keyID] = currKeyHash;
		load = 1;
		m_totalIndexKeyword++;
		return true;
	} else {
		currKeyHash = itKeyHash->second;
		size_t indexTotal = currKeyHash->m_Source_map.size();
		if (indexTotal > KADEMLIAMAXINDEX) {
			load = 100;
			// Too many entries for this Keyword..
			return false;
		}
		Source *currSource = NULL;
		CSourceKeyMap::iterator itSource = currKeyHash->m_Source_map.find(sourceID);
		if (itSource != currKeyHash->m_Source_map.end()) {
			currSource = itSource->second;
			if (!currSource->entryList.empty()) {
				if (indexTotal > KADEMLIAMAXINDEX - 5000) {
					load = 100;
					// We are in a hot node. Updating all the publishes while
					// this index is full would index nothing but popular files.
					return false;
				}
				// also check for size match
				CKeyEntry *oldEntry = NULL;
				for (CKadEntryPtrList::iterator itEntry = currSource->entryList.begin();
					itEntry != currSource->entryList.end();
					++itEntry) {
					CKeyEntry *currEntry = static_cast<Kademlia::CKeyEntry *>(*itEntry);
					wxASSERT(currEntry->IsKeyEntry());
					if (currEntry->m_uSize == entry->m_uSize) {
						oldEntry = currEntry;
						currSource->entryList.erase(itEntry);
						break;
					}
				}
				entry->MergeIPsAndFilenames(
					oldEntry); // oldEntry can be NULL, that's ok and we still need to do
						   // this call in this case
				if (oldEntry == NULL) {
					m_totalIndexKeyword++;
					AddDebugLogLineN(logKadIndex,
						"Multiple sizes published for file " +
							entry->m_uSourceID.ToHexString());
				}
				delete oldEntry;
				oldEntry = NULL;
			} else {
				m_totalIndexKeyword++;
				entry->MergeIPsAndFilenames(NULL); // IpTracking init
			}
			load = (uint8_t)((indexTotal * 100) / KADEMLIAMAXINDEX);
			currSource->entryList.push_front(entry);
			return true;
		} else {
			currSource = new Source;
			currSource->sourceID = sourceID;
			entry->MergeIPsAndFilenames(NULL); // IpTracking init
			currSource->entryList.push_front(entry);
			currKeyHash->m_Source_map[currSource->sourceID] = currSource;
			m_totalIndexKeyword++;
			load = (indexTotal * 100) / KADEMLIAMAXINDEX;
			return true;
		}
	}
}

bool CIndexed::AddSources(
	const CUInt128 &keyID, const CUInt128 &sourceID, Kademlia::CEntry *entry, uint8_t &load)
{
	if (!IsReady()) {
		return false;
	}
	if (!entry) {
		return false;
	}

	if (entry->m_uIP == 0 || entry->m_uTCPport == 0 || entry->m_uUDPport == 0 ||
		entry->GetTagCount() == 0 || entry->m_tLifeTime < time(NULL)) {
		return false;
	}

	SrcHash *currSrcHash = NULL;
	SrcHashMap::iterator itSrcHash = m_Sources_map.find(keyID);
	if (itSrcHash == m_Sources_map.end()) {
		Source *currSource = new Source;
		currSource->sourceID = sourceID;
		currSource->entryList.push_front(entry);
		currSrcHash = new SrcHash;
		currSrcHash->keyID = keyID;
		currSrcHash->m_Source_map.push_front(currSource);
		m_Sources_map[currSrcHash->keyID] = currSrcHash;
		m_totalIndexSource++;
		load = 1;
		return true;
	} else {
		currSrcHash = itSrcHash->second;
		size_t size = currSrcHash->m_Source_map.size();

		for (CKadSourcePtrList::iterator itSource = currSrcHash->m_Source_map.begin();
			itSource != currSrcHash->m_Source_map.end();
			++itSource) {
			Source *currSource = *itSource;
			if (!currSource->entryList.empty()) {
				CEntry *currEntry = currSource->entryList.front();
				wxASSERT(currEntry != NULL);
				if (currEntry->m_uIP == entry->m_uIP &&
					(currEntry->m_uTCPport == entry->m_uTCPport ||
						currEntry->m_uUDPport == entry->m_uUDPport)) {
					CEntry *currName = currSource->entryList.front();
					currSource->entryList.pop_front();
					delete currName;
					currSource->entryList.push_front(entry);
					load = (size * 100) / KADEMLIAMAXSOURCEPERFILE;
					return true;
				}
			} else {
				// This should never happen!
				currSource->entryList.push_front(entry);
				wxFAIL;
				load = (size * 100) / KADEMLIAMAXSOURCEPERFILE;
				return true;
			}
		}
		if (size > KADEMLIAMAXSOURCEPERFILE) {
			Source *currSource = currSrcHash->m_Source_map.back();
			currSrcHash->m_Source_map.pop_back();
			wxASSERT(currSource != NULL);
			Kademlia::CEntry *currName = currSource->entryList.back();
			currSource->entryList.pop_back();
			wxASSERT(currName != NULL);
			delete currName;
			currSource->sourceID = sourceID;
			currSource->entryList.push_front(entry);
			currSrcHash->m_Source_map.push_front(currSource);
			load = 100;
			return true;
		} else {
			Source *currSource = new Source;
			currSource->sourceID = sourceID;
			currSource->entryList.push_front(entry);
			currSrcHash->m_Source_map.push_front(currSource);
			m_totalIndexSource++;
			load = (size * 100) / KADEMLIAMAXSOURCEPERFILE;
			return true;
		}
	}

	return false;
}

bool CIndexed::AddNotes(
	const CUInt128 &keyID, const CUInt128 &sourceID, Kademlia::CEntry *entry, uint8_t &load)
{
	if (!IsReady()) {
		return false;
	}
	if (!entry) {
		return false;
	}

	if (entry->m_uIP == 0 || entry->GetTagCount() == 0) {
		return false;
	}

	SrcHash *currNoteHash = NULL;
	SrcHashMap::iterator itNoteHash = m_Notes_map.find(keyID);
	if (itNoteHash == m_Notes_map.end()) {
		Source *currNote = new Source;
		currNote->sourceID = sourceID;
		currNote->entryList.push_front(entry);
		currNoteHash = new SrcHash;
		currNoteHash->keyID = keyID;
		currNoteHash->m_Source_map.push_front(currNote);
		m_Notes_map[currNoteHash->keyID] = currNoteHash;
		load = 1;
		m_totalIndexNotes++;
		return true;
	} else {
		currNoteHash = itNoteHash->second;
		size_t size = currNoteHash->m_Source_map.size();

		for (CKadSourcePtrList::iterator itSource = currNoteHash->m_Source_map.begin();
			itSource != currNoteHash->m_Source_map.end();
			++itSource) {
			Source *currNote = *itSource;
			if (!currNote->entryList.empty()) {
				CEntry *currEntry = currNote->entryList.front();
				wxASSERT(currEntry != NULL);
				if (currEntry->m_uIP == entry->m_uIP ||
					currEntry->m_uSourceID == entry->m_uSourceID) {
					CEntry *currName = currNote->entryList.front();
					currNote->entryList.pop_front();
					delete currName;
					currNote->entryList.push_front(entry);
					load = (size * 100) / KADEMLIAMAXNOTESPERFILE;
					return true;
				}
			} else {
				// This should never happen!
				currNote->entryList.push_front(entry);
				wxFAIL;
				load = (size * 100) / KADEMLIAMAXNOTESPERFILE;
				m_totalIndexNotes++;
				return true;
			}
		}
		if (size > KADEMLIAMAXNOTESPERFILE) {
			Source *currNote = currNoteHash->m_Source_map.back();
			currNoteHash->m_Source_map.pop_back();
			wxASSERT(currNote != NULL);
			CEntry *currName = currNote->entryList.back();
			currNote->entryList.pop_back();
			wxASSERT(currName != NULL);
			delete currName;
			currNote->sourceID = sourceID;
			currNote->entryList.push_front(entry);
			currNoteHash->m_Source_map.push_front(currNote);
			load = 100;
			return true;
		} else {
			Source *currNote = new Source;
			currNote->sourceID = sourceID;
			currNote->entryList.push_front(entry);
			currNoteHash->m_Source_map.push_front(currNote);
			load = (size * 100) / KADEMLIAMAXNOTESPERFILE;
			m_totalIndexNotes++;
			return true;
		}
	}
}

bool CIndexed::AddLoad(const CUInt128 &keyID, uint32_t timet)
{
	if (!IsReady()) {
		return false;
	}
	Load *load = NULL;

	if ((uint32_t)time(NULL) > timet) {
		return false;
	}

	LoadMap::iterator it = m_Load_map.find(keyID);
	if (it != m_Load_map.end()) {
		wxFAIL;
		return false;
	}

	load = new Load();
	load->keyID = keyID;
	load->time = timet;
	m_Load_map[load->keyID] = load;
	m_totalIndexLoad++;
	return true;
}

void CIndexed::SendValidKeywordResult(const CUInt128 &keyID,
	const SSearchTerm *pSearchTerms,
	uint32_t ip,
	uint16_t port,
	bool oldClient,
	uint16_t startPosition,
	const CKadUDPKey &senderKey)
{
	if (!IsReady()) {
		return;
	}
	KeyHash *currKeyHash = NULL;
	KeyHashMap::iterator itKeyHash = m_Keyword_map.find(keyID);
	if (itKeyHash != m_Keyword_map.end()) {
		currKeyHash = itKeyHash->second;
		CMemFile packetdata(1024 * 50);
		packetdata.WriteUInt128(Kademlia::CKademlia::GetPrefs()->GetKadID());
		packetdata.WriteUInt128(keyID);
		packetdata.WriteUInt16(50);
		const uint16_t maxResults = 300;
		int count = 0 - startPosition;

		// Two loops: the first ignores results with a trustvalue below 1, the second also
		// considers those. That keeps the 300 max results from filling with spam. Sorting
		// by trustvalue instead would risk sending only popular files on very hot keywords.
		bool onlyTrusted = true;
		DEBUG_ONLY(uint32_t dbgResultsTrusted = 0;)
		DEBUG_ONLY(uint32_t dbgResultsUntrusted = 0;)

		do {
			for (CSourceKeyMap::iterator itSource = currKeyHash->m_Source_map.begin();
				itSource != currKeyHash->m_Source_map.end();
				++itSource) {
				Source *currSource = itSource->second;

				for (CKadEntryPtrList::iterator itEntry = currSource->entryList.begin();
					itEntry != currSource->entryList.end();
					++itEntry) {
					Kademlia::CKeyEntry *currName =
						static_cast<Kademlia::CKeyEntry *>(*itEntry);
					wxASSERT(currName->IsKeyEntry());
					if ((onlyTrusted ^ (currName->GetTrustValue() < 1.0)) &&
						(!pSearchTerms || currName->SearchTermsMatch(pSearchTerms))) {
						if (count < 0) {
							count++;
						} else if ((uint16_t)count < maxResults) {
							if (!oldClient ||
								currName->m_uSize <= OLD_MAX_FILE_SIZE) {
								count++;
#ifdef __DEBUG__
								if (onlyTrusted) {
									dbgResultsTrusted++;
								} else {
									dbgResultsUntrusted++;
								}
#endif
								packetdata.WriteUInt128(
									currName->m_uSourceID);
								currName->WriteTagListWithPublishInfo(
									&packetdata);
								if (count % 50 == 0) {
									DebugSend(Kad2SearchRes, ip, port);
									CKademlia::GetUDPListener()
										->SendPacket(packetdata,
											KADEMLIA2_SEARCH_RES,
											ip,
											port,
											senderKey,
											NULL);
									// Reset the packet, keeping
									// the header (Kad id, key
									// id, entry count).
									packetdata.SetLength(16 + 16 + 2);
								}
							}
						} else {
							itSource = currKeyHash->m_Source_map.end();
							--itSource;
							break;
						}
					}
				}
			}

			if (onlyTrusted && count < (int)maxResults) {
				onlyTrusted = false;
			} else {
				break;
			}
		} while (!onlyTrusted);

		AddDebugLogLineN(logKadIndex,
			CFormat("Kad keyword search result request: Sent %u trusted and %u untrusted "
				"results") %
				dbgResultsTrusted % dbgResultsUntrusted);

		if (count > 0) {
			uint16_t countLeft = (uint16_t)count % 50;
			if (countLeft) {
				packetdata.Seek(16 + 16);
				packetdata.WriteUInt16(countLeft);
				DebugSend(Kad2SearchRes, ip, port);
				CKademlia::GetUDPListener()->SendPacket(
					packetdata, KADEMLIA2_SEARCH_RES, ip, port, senderKey, NULL);
			}
		}
	}
	Clean();
}

void CIndexed::SendValidSourceResult(const CUInt128 &keyID,
	uint32_t ip,
	uint16_t port,
	uint16_t startPosition,
	uint64_t fileSize,
	const CKadUDPKey &senderKey)
{
	if (!IsReady()) {
		return;
	}
	SrcHash *currSrcHash = NULL;
	SrcHashMap::iterator itSrcHash = m_Sources_map.find(keyID);
	if (itSrcHash != m_Sources_map.end()) {
		currSrcHash = itSrcHash->second;
		CMemFile packetdata(1024 * 50);
		packetdata.WriteUInt128(Kademlia::CKademlia::GetPrefs()->GetKadID());
		packetdata.WriteUInt128(keyID);
		packetdata.WriteUInt16(50);
		uint16_t maxResults = 300;
		int count = 0 - startPosition;

		for (CKadSourcePtrList::iterator itSource = currSrcHash->m_Source_map.begin();
			itSource != currSrcHash->m_Source_map.end();
			++itSource) {
			Source *currSource = *itSource;
			if (!currSource->entryList.empty()) {
				Kademlia::CEntry *currName = currSource->entryList.front();
				if (count < 0) {
					count++;
				} else if (count < maxResults) {
					if (!fileSize || !currName->m_uSize ||
						currName->m_uSize == fileSize) {
						packetdata.WriteUInt128(currName->m_uSourceID);
						currName->WriteTagList(&packetdata);
						count++;
						if (count % 50 == 0) {
							DebugSend(Kad2SearchRes, ip, port);
							CKademlia::GetUDPListener()->SendPacket(packetdata,
								KADEMLIA2_SEARCH_RES,
								ip,
								port,
								senderKey,
								NULL);
							// Reset the packet, keeping the header (Kad id, key
							// id, number of entries)
							packetdata.SetLength(16 + 16 + 2);
						}
					}
				} else {
					break;
				}
			}
		}

		if (count > 0) {
			uint16_t countLeft = (uint16_t)count % 50;
			if (countLeft) {
				packetdata.Seek(16 + 16);
				packetdata.WriteUInt16(countLeft);
				DebugSend(Kad2SearchRes, ip, port);
				CKademlia::GetUDPListener()->SendPacket(
					packetdata, KADEMLIA2_SEARCH_RES, ip, port, senderKey, NULL);
			}
		}
	}
	Clean();
}

void CIndexed::SendValidNoteResult(
	const CUInt128 &keyID, uint32_t ip, uint16_t port, uint64_t fileSize, const CKadUDPKey &senderKey)
{
	if (!IsReady()) {
		return;
	}
	SrcHash *currNoteHash = NULL;
	SrcHashMap::iterator itNote = m_Notes_map.find(keyID);
	if (itNote != m_Notes_map.end()) {
		currNoteHash = itNote->second;
		CMemFile packetdata(1024 * 50);
		packetdata.WriteUInt128(Kademlia::CKademlia::GetPrefs()->GetKadID());
		packetdata.WriteUInt128(keyID);
		packetdata.WriteUInt16(50);
		uint16_t maxResults = 150;
		uint16_t count = 0;

		for (CKadSourcePtrList::iterator itSource = currNoteHash->m_Source_map.begin();
			itSource != currNoteHash->m_Source_map.end();
			++itSource) {
			Source *currNote = *itSource;
			if (!currNote->entryList.empty()) {
				Kademlia::CEntry *currName = currNote->entryList.front();
				if (count < maxResults) {
					if (!fileSize || !currName->m_uSize ||
						fileSize == currName->m_uSize) {
						packetdata.WriteUInt128(currName->m_uSourceID);
						currName->WriteTagList(&packetdata);
						count++;
						if (count % 50 == 0) {
							DebugSend(Kad2SearchRes, ip, port);
							CKademlia::GetUDPListener()->SendPacket(packetdata,
								KADEMLIA2_SEARCH_RES,
								ip,
								port,
								senderKey,
								NULL);
							// Reset the packet, keeping the header (Kad id, key
							// id, number of entries)
							packetdata.SetLength(16 + 16 + 2);
						}
					}
				} else {
					break;
				}
			}
		}

		uint16_t countLeft = count % 50;
		if (countLeft) {
			packetdata.Seek(16 + 16);
			packetdata.WriteUInt16(countLeft);
			DebugSend(Kad2SearchRes, ip, port);
			CKademlia::GetUDPListener()->SendPacket(
				packetdata, KADEMLIA2_SEARCH_RES, ip, port, senderKey, NULL);
		}
	}
}

bool CIndexed::SendStoreRequest(const CUInt128 &keyID)
{
	// The load map only throttles outgoing publishing.
	if (!IsReady()) {
		return true;
	}
	Load *load = NULL;
	LoadMap::iterator it = m_Load_map.find(keyID);
	if (it != m_Load_map.end()) {
		load = it->second;
		if (load->time < (uint32_t)time(NULL)) {
			m_Load_map.erase(it);
			m_totalIndexLoad--;
			delete load;
			return true;
		}
		return false;
	}
	return true;
}

SSearchTerm::SSearchTerm()
: type(AND)
, tag(NULL)
, astr(NULL)
, left(NULL)
, right(NULL)
{
}

SSearchTerm::~SSearchTerm()
{
	if (type == String) {
		delete astr;
	}
	delete tag;
}
// File_checked_for_headers
