# Kad contact distribution

The Kad panel displays routing contacts in 4096 bins, selected by the first twelve bits of the contact's absolute KadID. Blue bars show all contacts; green bars show contacts whose addresses are marked verified. The summary reports total contacts, verified contacts, and distinct IPv4 `/24` subnets.

Bins are summed into screen columns, so resolution follows panel width. A foreground marker shows the local KadID. The chart scales to the largest column and refreshes once a second while visible, independently of the existing node-history graph's update preference. It reads a value snapshot rather than retaining routing contact pointers. Stopping Kad clears the distribution. Identical snapshots do not trigger repainting; resize events do. Layout accommodates DPI scaling and large count labels. The chart reserves space for its wrapped summary, axis labels and a minimum plot area, and recalculates its minimum size on resize, font and DPI changes. Words wider than the summary area are split at Unicode characters. Restoring the main window refreshes the visible chart immediately; amulegui also requests a snapshot as polling starts or reconnects. Clustering near the local KadID is expected; the chart does not classify or ban nodes.

amulegui requests the snapshot only while the Kad panel is visible, by including an empty `EC_TAG_STATS_KAD_DISTRIBUTION` request tag. Other statistics requests incur no distribution scan. The snapshot returns through the optional `EC_TAG_STATS_KAD_DISTRIBUTION` (`0x021E`) statistics tag. The view begins in a neutral loading state. Local snapshots are filled immediately; amulegui requests a snapshot when the panel appears and refreshes directly on the reply. Replies to requests without the tag leave the distribution state unchanged. Only a reply to an opted-in request can mark the core as unsupported (missing, invalid or unsupported-version data).

The sparse version-2 payload is `12 + 10 * nonempty_bins` bytes, including only populated bins while preserving 32-bit counts:

| Offset | Field | Encoding |
| --- | --- | --- |
| 0 | Version (`2`) | uint8 |
| 1 | Local KadID present (`0` or `1`) | uint8 |
| 2 | Local KadID's most significant word | big-endian uint32 |
| 6 | Distinct IPv4 /24 subnet count | big-endian uint32 |
| 10 | Number of entries | big-endian uint16 |
| 12 onward | Bin index, contact count, verified count | big-endian uint16, uint32, uint32 |

Entries use strictly increasing indices in `[0, 4095]`. Decode rejects unknown flags/versions, malformed lengths, duplicate or unordered/out-of-range indices, empty entries, impossible verified/subnet counts and total-count overflow. An empty snapshot is 12 bytes; a single populated bin is 22 bytes; the maximum is 40,972 bytes. No count is truncated to 16 bits. The subnet count comes from the routing bin's existing global tracking map, whose zero-count entries are erased.

The behavior was informed by eMule's contact histogram in reference checkout `ac3d52e`. The snapshot builder, portable codec, and wxWidgets view were written for aMule; no MFC control or reference implementation was transplanted.

The populated EC integration test seeds a contact bound to a Kad-compatible IPv4 address owned by the test machine. It verifies the contact’s absolute-ID bin, verification flag, subnet count, unchanged data after an authenticated client reconnects, omission for legacy statistics requests, and clearing when Kad stops. It skips if the machine has no suitable address. The empty-state EC and codec tests do not depend on that address.
