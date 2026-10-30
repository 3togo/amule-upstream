// This file is part of the aMule Project.
// Copyright (c) 2003-2026 aMule Team ( https://amule-org.github.io )
// Licensed under the GNU General Public License, version 2 or later.
#include <muleunit/test.h>
#include <kademlia/utils/KadConstraintsConfig.h>
#include <wx/fileconf.h>
#include <wx/sstream.h>
using namespace muleunit;
using namespace Kademlia;
DECLARE_SIMPLE(KadConstraints)

TEST(KadConstraints, DefaultsDeriveGlobalBudgetsFromApplicationSettings)
{
	wxStringInputStream empty("");
	wxFileConfig cfg(empty);
	const auto policy = LoadKadConstraints(cfg, 100, 3);
	ASSERT_EQUALS(uint32_t(15), policy.globalLookupActive);
	ASSERT_EQUALS(policy.outgoingRequests, policy.globalLookupPending);
	ASSERT_TRUE(policy.lookupConcurrency <= policy.globalLookupActive);
	ASSERT_TRUE(policy.lookupPending <= policy.globalLookupPending);
	SaveKadConstraints(cfg, policy);
	ASSERT_EQUALS(wxString("0"), cfg.Read("/Kad/LookupActiveGlobal"));
	ASSERT_EQUALS(wxString("0"), cfg.Read("/Kad/LookupPendingGlobal"));
	const auto changed = LoadKadConstraints(cfg, 10, 3);
	ASSERT_EQUALS(uint32_t(10), changed.globalLookupActive);
}

TEST(KadConstraints, InvalidValuesCannotWrapOrDisableBounds)
{
	for (const wxString &bad : { wxString("0"),
		     wxString("-1"),
		     wxString(" -18446744073709551615 "),
		     wxString("4294967296"),
		     wxString("nonsense") }) {
		wxStringInputStream empty("");
		wxFileConfig cfg(empty);
		cfg.Write("/Kad/TrackedIPs", bad);
		const auto policy = LoadKadConstraints(cfg, 100, 3);
		ASSERT_EQUALS(SKadConstraints().trackedIPs, policy.trackedIPs);
	}
	wxStringInputStream empty("");
	wxFileConfig cfg(empty);
	cfg.Write("/Kad/SubnetPrefix", wxString("33"));
	cfg.Write("/Kad/PingRequestsPerMinute", wxString("60001"));
	cfg.Write("/Kad/FloodMultiplier", wxString("4294967295"));
	const auto policy = LoadKadConstraints(cfg, 100, 3);
	ASSERT_EQUALS(SKadConstraints().subnetPrefix, policy.subnetPrefix);
	ASSERT_EQUALS(SKadConstraints().pingRate, policy.pingRate);
	ASSERT_EQUALS(SKadConstraints().floodMultiplier, policy.floodMultiplier);
}

TEST(KadConstraints, CustomConstraintsAreCoherentAndPersist)
{
	wxStringInputStream empty("");
	wxFileConfig cfg(empty);
	cfg.Write("/Kad/OutgoingRequests", wxString("7"));
	cfg.Write("/Kad/LookupPendingGlobal", wxString("20"));
	cfg.Write("/Kad/LookupActiveGlobal", wxString("12"));
	cfg.Write("/Kad/LookupPendingPerSearch", wxString("3"));
	cfg.Write("/Kad/LookupConcurrency", wxString("9"));
	cfg.Write("/Kad/NodeConcurrency", wxString("8"));
	cfg.Write("/Kad/SubnetPrefix", wxString("16"));
	cfg.Write("/Kad/PingRequestsPerMinute", wxString("7"));
	const auto policy = LoadKadConstraints(cfg, 100, 3);
	ASSERT_EQUALS(uint32_t(7), policy.globalLookupPending);
	ASSERT_EQUALS(uint32_t(7), policy.globalLookupActive);
	ASSERT_EQUALS(uint32_t(3), policy.lookupConcurrency);
	ASSERT_EQUALS(uint32_t(3), policy.nodeConcurrency);
	ASSERT_EQUALS(uint32_t(0x01020000), policy.Subnet(0x01020304));
	ASSERT_EQUALS(uint32_t(7), policy.pingRate);
	SaveKadConstraints(cfg, policy);
	wxStringOutputStream saved;
	cfg.Save(saved);
	wxStringInputStream input(saved.GetString());
	wxFileConfig restored(input);
	const auto roundTrip = LoadKadConstraints(restored, 100, 3);
	for (const auto &setting : SKadConstraints::Settings()) {
		ASSERT_EQUALS(policy.*(setting.member), roundTrip.*(setting.member));
	}
}
