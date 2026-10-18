//
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
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
//

#include <muleunit/test.h>
#include "Metrics.h"
#include "State.h"
#include <locale>

using namespace muleunit;
using namespace webapi;
DECLARE_SIMPLE(Metrics)

namespace
{
class GroupedNumbers : public std::numpunct<char>
{
	char do_thousands_sep() const override { return '.'; }
	std::string do_grouping() const override { return "\3"; }
	char do_decimal_point() const override { return ','; }
};
struct LocaleGuard
{
	std::locale previous = std::locale();
	LocaleGuard() { std::locale::global(std::locale(previous, new GroupedNumbers)); }
	~LocaleGuard() { std::locale::global(previous); }
};
} // namespace

TEST(Metrics, EscapesLabelsAndIgnoresGlobalNumberLocale)
{
	MetricsSnapshot s;
	s.ec_connected = true;
	s.snapshot_at = 1234567;
	s.status.total_sent_bytes = { 123456789012345ULL, true };
	LocaleGuard guard;
	const auto text = SerializeMetrics(s, "version\\\"\nnext");
	ASSERT_TRUE(
		text.find("amule_build_info{version=\"version\\\\\\\"\\nnext\"} 1\n") != std::string::npos);
	ASSERT_TRUE(text.find("amule_snapshot_timestamp_seconds 1234567\n") != std::string::npos);
	ASSERT_TRUE(text.find("amule_transferred_bytes_total{direction=\"upload\"} 123456789012345\n") !=
		    std::string::npos);
	ASSERT_EQUALS('\n', text.back());
}

TEST(Metrics, OmitsUnavailableMeasurementsButRetainsRealZero)
{
	MetricsSnapshot s;
	s.ec_connected = true;
	s.snapshot_at = 1;
	const auto missing = SerializeMetrics(s, "test");
	ASSERT_TRUE(missing.find("amule_disk_free_bytes") == std::string::npos);
	ASSERT_TRUE(missing.find("amule_kad_network_users") == std::string::npos);
	ASSERT_TRUE(missing.find("amule_ed2k_high_id") == std::string::npos);
	ASSERT_TRUE(missing.find("amule_transferred_bytes_total") == std::string::npos);
	ASSERT_TRUE(missing.find("amule_shared_files") == std::string::npos);
	s.status.temp_free_bytes = 0;
	s.status.total_received_bytes = { 0, true };
	s.status.download_limit_bytes_per_second = { 0, true };
	const auto zero = SerializeMetrics(s, "test");
	ASSERT_TRUE(zero.find("amule_disk_free_bytes{dir=\"temp\"} 0\n") != std::string::npos);
	ASSERT_TRUE(
		zero.find("amule_transferred_bytes_total{direction=\"download\"} 0\n") != std::string::npos);
	ASSERT_TRUE(zero.find("amule_speed_limit_bytes_per_second{direction=\"download\"} 0\n") !=
		    std::string::npos);
}

TEST(Metrics, PublishesOnlySuccessfulTicksAndSuppressesStaleMeasurements)
{
	CState state;
	ASSERT_TRUE(SerializeMetrics(state.Metrics(), "test").find("amule_snapshot_timestamp_seconds") ==
		    std::string::npos);
	StatusSnapshot status;
	status.download_bytes_per_second = 123;
	state.WriteStatus(status);
	FileMap files;
	FileSnapshot download;
	download.ecid = 1;
	download.is_downloading = true;
	download.download.status = "paused";
	files.emplace(1, download);
	download.ecid = 2;
	download.download.status = "arbitrary future state";
	files.emplace(2, download);
	download.ecid = 3;
	download.is_downloading = false;
	download.is_shared = true;
	files.emplace(3, download);
	MetricsSnapshot candidate;
	candidate.status = status;
	candidate.CountDownloads(files);
	state.MutateDownloads([&](FileMap &cache) { cache = std::move(files); });
	StatusSnapshot interleaved;
	interleaved.download_bytes_per_second = 456;
	state.WriteStatus(interleaved); // another tick updates the live cache before publication
	state.MarkTickSuccess(&candidate);
	const auto first = state.Metrics();
	ASSERT_EQUALS(static_cast<std::uint64_t>(123), first.status.download_bytes_per_second);
	const auto text = SerializeMetrics(first, "test");
	ASSERT_TRUE(text.find("amule_downloads{status=\"paused\"} 1\n") != std::string::npos);
	ASSERT_TRUE(text.find("amule_downloads{status=\"unknown\"} 1\n") != std::string::npos);
	ASSERT_TRUE(text.find("arbitrary future state") == std::string::npos);
	status.download_bytes_per_second = 999;
	state.WriteStatus(status); // partial next tick must not leak into scrapes
	ASSERT_EQUALS(static_cast<std::uint64_t>(123), state.Metrics().status.download_bytes_per_second);
	state.MarkTickFailure();
	const auto failed = state.Metrics();
	ASSERT_FALSE(failed.ec_connected);
	ASSERT_EQUALS(first.snapshot_at, failed.snapshot_at);
	const auto offline = SerializeMetrics(failed, "test");
	ASSERT_TRUE(offline.find("amule_ec_connected 0\n") != std::string::npos);
	ASSERT_TRUE(offline.find("amule_transfer_rate_bytes_per_second") == std::string::npos);
	candidate.status = status;
	state.MarkTickSuccess(&candidate);
	ASSERT_EQUALS(static_cast<std::uint64_t>(999), state.Metrics().status.download_bytes_per_second);
	ASSERT_TRUE(state.Metrics().ec_connected);
}

TEST(Metrics, DisabledCollectionDoesNotPublish)
{
	CState state;
	state.MarkTickSuccess();
	ASSERT_EQUALS(static_cast<std::time_t>(0), state.Metrics().snapshot_at);
}

TEST(Metrics, ConnectedNetworkGatesExposeRealZeroValues)
{
	MetricsSnapshot s;
	s.ec_connected = true;
	s.snapshot_at = 1;
	s.status.ed2k_state = "connected";
	s.status.kad_state = "connected";
	s.status.has_ed2k_network = true;
	s.status.ed2k_high_id = true;
	s.status.has_kad_firewalled_tcp = true;
	s.kad.has_firewalled_udp = true;
	s.kad.firewalled_udp = true;
	s.kad.has_network = true;
	s.kad.has_indexed = true;
	const auto text = SerializeMetrics(s, "test");
	ASSERT_TRUE(text.find("amule_ed2k_high_id 1\n") != std::string::npos);
	ASSERT_TRUE(text.find("amule_ed2k_network_users 0\n") != std::string::npos);
	ASSERT_TRUE(text.find("amule_kad_network_nodes 0\n") != std::string::npos);
	ASSERT_TRUE(text.find("amule_kad_firewalled{protocol=\"tcp\"} 0\n") != std::string::npos);
	ASSERT_TRUE(text.find("amule_kad_firewalled{protocol=\"udp\"} 1\n") != std::string::npos);
	ASSERT_TRUE(text.find("amule_kad_indexed{type=\"sources\"} 0\n") != std::string::npos);
	ASSERT_TRUE(text.find("amule_kad_index_load 0\n") != std::string::npos);
	ASSERT_TRUE(text.find("amule_kad_indexed{type=\"load\"}") == std::string::npos);
}
