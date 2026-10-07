# Kad contact distribution

The Kad panel displays routing contacts in 64 bins, selected by the first six bits of the contact's absolute KadID. Blue bars show all contacts; green bars show contacts whose addresses are marked verified. The summary reports total contacts, verified contacts, and distinct IPv4 `/24` subnets.

The chart scales to the largest bin and refreshes once a second while visible, independently of the existing node-history graph's update preference. It reads a value snapshot rather than retaining routing contact pointers. Stopping Kad clears the distribution. Identical snapshots do not trigger repainting; resize events do. Layout accommodates DPI scaling and large count labels. Clustering near the local KadID is expected; the chart does not classify or ban nodes.

amulegui requests the snapshot only while the Kad panel is visible, by including an empty `EC_TAG_STATS_KAD_DISTRIBUTION` request tag. Other statistics requests incur no distribution scan. The snapshot returns through the optional `EC_TAG_STATS_KAD_DISTRIBUTION` (`0x021E`) statistics tag. Missing or invalid data displays an unavailable message rather than implying zero contacts. The version-1 payload is 517 bytes: a version byte, 64 pairs of big-endian uint32 contact and verified counts, then a big-endian uint32 distinct-subnet count. Decode rejects unsupported versions, malformed lengths, impossible verified/subnet counts, and total-count overflow.

The behavior was informed by eMule's contact histogram in reference checkout `ac3d52e`. The snapshot builder, portable codec, and wxWidgets view were written for aMule; no MFC control or reference implementation was transplanted.
