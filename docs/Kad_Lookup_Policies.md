# Kad lookup diversity and request deadlines

## Lookup diversity and scheduling

A lookup remembers admitted IPs and /24 counts across all routing responses,
including its initial routing-table seeds. New referrals must use unique IPs
and may not raise a public /24 above two admitted addresses. Seeds are retained
even if their existing subnet count exceeds that limit. LAN addresses retain
the subnet exemption, but still require distinct IPs.

Each sent routing request has an endpoint and a monotonic deadline fixed at
send time. A reply from another endpoint does not postpone that deadline.
There are at most `ALPHA_QUERY` requests before their deadlines (one for a
`NODE` lookup), with at most 64 pending records including late-response records.
The periodic fallback sends at most one new routing request per lookup per
pass. Immediate closer-node queries also respect that concurrency budget.
Soft timeouts free slots without banning the peer; records and candidates
remain available for another ten seconds to accept late replies and preserve
round-trip measurements. Duplicate, unsolicited and oversized responses do not
complete those records. A deferred or failed reask does not consume the widening
budget.

Without `ENABLE_KAD_NODE_PROTECTION`, deadlines use three seconds. With it,
interactive requests use the existing adaptive estimator. Background
publication keeps three-second deadlines and its three-second quiet interval.
The optional identity protections retain their existing build default.

## Reference influence

The lookup-wide subnet history and distinction between soft timeout and late
response handling were informed by [libtorrent's traversal implementation at
3da533d](https://github.com/arvidn/libtorrent/blob/3da533d27e3c66af75aedc0ff7d44d68622a7e2b/src/kademlia/traversal_algorithm.cpp).
Libtorrent uses a different DHT protocol; its packets, subnet thresholds and
branching rules were not transplanted.

## Validation

`KadLookupPolicyTest` covers diversity across replies, seeds, LAN exceptions,
independent deadlines, endpoint matching, late samples, duplicate completion,
pending-record bounds and expiry boundaries. Existing daemon EC lookup and
index lifecycle tests exercise diagnostics and publication/search compatibility.

The [public-network comparison](benchmarks/2026-10-30-kad-live/README.md) records
24 paired searches from the original workspace implementation: faster first
results on its warmer second pass, but no established overall improvement and
lower total result yield. Its exact source/binary fingerprints are retained;
it predates adaptation of this PR onto the newer upstream revision.
