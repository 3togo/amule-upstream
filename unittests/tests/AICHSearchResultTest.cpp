// This file is part of the aMule Project.
// Copyright (c) 2026 aMule Team ( https://amule-org.github.io )
// Licensed under the GNU General Public License, version 2 or later.

#include <muleunit/test.h>
#include <SHAHashSet.h>
#include <Preferences.h>
#include <kademlia/kademlia/AICHHashList.h>

using namespace muleunit;
using Kademlia::CKadAICHHashList;

DECLARE_SIMPLE(AICHSearchResult)

// The test uses the real hashset and consensus. It never creates a client or
// recovery request; the empty global request list still needs this destructor hook.
void CClientRef::Unlink()
{
	ASSERT_TRUE(m_client == nullptr);
}

bool CPreferences::s_AICHTrustEveryHash = false;

static CAICHHash MakeRoot(uint8_t seed)
{
	CAICHHash hash;
	memset(hash.GetRawHash(), seed, CAICHHash::GetHashSize());
	return hash;
}

TEST(AICHSearchResult, FabricatedCountsContributeOnlyOneVote)
{
	for (uint8_t claimed : { 2, 3, 255 }) {
		std::vector<uint8_t> payload(2 + CAICHHash::GetHashSize(), 0xAB);
		payload[0] = 1;
		payload[1] = claimed;
		std::vector<CKadAICHHashList::SResultHash> decoded;
		ASSERT_TRUE(CKadAICHHashList::DecodeResultTag(payload.data(), payload.size(), decoded));
		const auto *candidate = CKadAICHHashList::SelectCandidate(decoded, claimed);
		ASSERT_TRUE(candidate != nullptr);
		CAICHHash root;
		memcpy(root.GetRawHash(), candidate->m_hash.data(), CAICHHash::GetHashSize());
		CAICHHashSet hashes(nullptr);
		// Peer byte order for 1.2.3.4. The Kad call site swaps its address once.
		const uint32_t responder = 0x04030201;
		hashes.SearchResultHashReceived(root, true, responder);
		ASSERT_EQUALS(AICH_UNTRUSTED, hashes.GetStatus());
		for (unsigned i = 0; i < 20; ++i) {
			hashes.SearchResultHashReceived(root, true, responder);
			// Another address in the same /20 is also not an independent vote.
			hashes.UntrustedHashReceived(root, 0x05030201);
		}
		ASSERT_EQUALS(AICH_UNTRUSTED, hashes.GetStatus());
		for (uint32_t i = 2; i <= 9; ++i) {
			hashes.UntrustedHashReceived(root, 0x04030200 | i);
			ASSERT_EQUALS(AICH_UNTRUSTED, hashes.GetStatus());
		}
		hashes.UntrustedHashReceived(root, 0x0403020A);
		ASSERT_EQUALS(AICH_TRUSTED, hashes.GetStatus());
		ASSERT_TRUE(hashes.GetMasterHash() == root);
	}
}

TEST(AICHSearchResult, ConflictingReportsStillRequireAgreement)
{
	CAICHHashSet hashes(nullptr);
	const CAICHHash candidate = MakeRoot(0xAB);
	const CAICHHash other = MakeRoot(0xCD);
	hashes.SearchResultHashReceived(candidate, true, 0x04030201);
	for (uint32_t i = 2; i <= 11; ++i) {
		hashes.UntrustedHashReceived(other, 0x04030200 | i);
	}
	// Ten of eleven is below the existing 92% threshold.
	ASSERT_EQUALS(AICH_UNTRUSTED, hashes.GetStatus());
	hashes.UntrustedHashReceived(other, 0x0403020C);
	ASSERT_EQUALS(AICH_UNTRUSTED, hashes.GetStatus());
	hashes.UntrustedHashReceived(other, 0x0403020D);
	ASSERT_EQUALS(AICH_TRUSTED, hashes.GetStatus());
	ASSERT_TRUE(hashes.GetMasterHash() == other);
}

TEST(AICHSearchResult, MissingProvenanceDoesNotVoteAndServerPolicyIsUnchanged)
{
	const CAICHHash root = MakeRoot(0xAB);
	CAICHHashSet kad(nullptr);
	kad.SearchResultHashReceived(root, true, 0);
	ASSERT_EQUALS(AICH_EMPTY, kad.GetStatus());
	ASSERT_FALSE(kad.HasValidMasterHash());

	CAICHHashSet server(nullptr);
	server.SearchResultHashReceived(root, false, 0);
	ASSERT_EQUALS(AICH_TRUSTED, server.GetStatus());
	ASSERT_TRUE(server.GetMasterHash() == root);
}

TEST(AICHSearchResult, VerifiedRootCannotBeReplacedByKad)
{
	CAICHHashSet hashes(nullptr);
	const CAICHHash verified = MakeRoot(0xAB);
	hashes.SetMasterHash(verified, AICH_VERIFIED);
	hashes.SearchResultHashReceived(MakeRoot(0xCD), true, 0x04030201);
	ASSERT_EQUALS(AICH_VERIFIED, hashes.GetStatus());
	ASSERT_TRUE(hashes.GetMasterHash() == verified);
}
