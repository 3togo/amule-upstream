# Kad packet tracking and flood penalties

## Packet tracking

Incoming request tracking holds at most 10,000 IPs. An ordered expiry index
reclaims expired entries before admission; when every slot is live, requests
from new addresses are dropped without allocating state or evicting existing
quotas. Replies still pass this request limiter. Each address has only the
fixed set of recognized opcode budgets; the legacy firewall opcodes share one
budget. Packet debt saturates, so a flood cannot overflow counters or leave an
arbitrarily long retention period after it stops.

Outgoing request tracking retains at most 4,096 requests, and legacy challenge
tracking retains at most 1,024 challenges. Both discard the oldest records
under pressure and reject expired responses at the 180-second boundary.

All incoming requests consume the drop budget. A separate budget counts only
requests carrying a valid receiver key before escalating to a massive flood.
Unkeyed traffic cannot prime the ban budget for a subsequent legitimate keyed
request. A massive keyed flood invokes the existing client ban and marks the
matching IP/UDP-port routing contact expired. Routing selection excludes that
contact immediately; ordinary routing cleanup deletes it when no search holds
a reference. Receiver keys mitigate off-path spoofing; they do not authenticate
Kad identities.

## Validation and reference influence

`KadPacketTrackingTest` exercises production tracking and routing-bin code with
a deterministic clock: capacity pressure, live quota preservation, expiry,
spoofed versus keyed traffic, LAN behavior, request/challenge bounds and routing
exclusion while a search reference remains alive.

Expiring a routing contact after a massive flood was informed by [the verified
eMule implementation at ac3d52e](https://github.com/irwir/eMule/blob/ac3d52eafd88e9f6d9e0c2204c6dbead18db750b/srchybrid/kademlia/net/KademliaUDPListener.cpp).
The separate keyed penalty budget is a local safeguard; no reference code was
copied. These changes affect local policy and add no Kad wire fields.
