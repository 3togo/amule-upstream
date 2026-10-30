# Application-wide Kad constraints

Local resource and timing policy is loaded from the `[Kad]` section of
`amule.conf`. Restart the core/daemon after editing these settings. The remote
GUI does not load or save the core's Kad settings. `SKadConstraints::Settings()`
is the single schema for names, defaults and supported representations.
Packet tracking, routing admission and lookup policy consume the same validated
policy. Wire opcodes, packet-size contracts and Kad identifier widths remain
protocol constants.

| Key | Default |
|---|---:|
| TrackedIPs | 10000 |
| OutgoingRequests | 4096 |
| LegacyChallenges | 1024 |
| TrackingLifetimeMs | 180000 |
| FloodMultiplier | 5 |
| BootstrapRequestsPerMinute | 2 |
| HelloRequestsPerMinute | 3 |
| RoutingRequestsPerMinute | 10 |
| SearchRequestsPerMinute | 3 |
| PublishKeywordRequestsPerMinute | 3 |
| PublishSourceRequestsPerMinute | 2 |
| PublishNotesRequestsPerMinute | 2 |
| FirewallRequestsPerMinute | 2 |
| BuddyRequestsPerMinute | 2 |
| CallbackRequestsPerMinute | 1 |
| PingRequestsPerMinute | 2 |
| ContactsPerIP | 1 |
| ContactsPerSubnet | 2 |
| GlobalContactsPerSubnet | 10 |
| SubnetPrefix | 24 |
| LookupConcurrency | 5 |
| NodeConcurrency | 1 |
| LookupPendingPerSearch | 64 |
| LookupActiveGlobal | 0 |
| LookupPendingGlobal | 0 |
| RequestTimeoutMs | 3000 |
| LateReplyGraceMs | 10000 |
| WideningReasks | 4 |

Times use milliseconds; request rates use packets per minute. An omitted,
malformed, negative, overflowing or invalid-zero value falls back to its default.
Capacity fields must be positive and fit a signed 32-bit count. Times must be
positive and fit unsigned 32-bit milliseconds. Rates range from 1 to 60,000 so
the millisecond accounting interval never becomes zero. Flood multipliers are
bounded by the representable counter product. Subnet prefixes range from 1 to
32. `WideningReasks=0` disables optional widening.

`LookupActiveGlobal=0` derives the aggregate active-request allowance from
`LookupConcurrency * KadMaxSourceSearches`, capped by `MaxConnections`.
`LookupPendingGlobal=0` derives the aggregate retained-request allowance from
`OutgoingRequests`. Automatic selections remain zero when saved, so changing
the application budgets recalculates them on the next start.

Explicit aggregate pending capacity cannot exceed outgoing tracking capacity;
aggregate active capacity cannot exceed aggregate pending capacity. Per-search
pending/concurrency and node concurrency are reduced to fit those budgets.
Subnet and IP allowances are reduced to fit the enclosing subnet allowance.
Normalized explicit values are saved, and no zero value disables resource
bounds. A search-wide helper cap is not a substitute for aggregate accounting.

`ContactsPerIP`, `ContactsPerSubnet`, `GlobalContactsPerSubnet` and `SubnetPrefix`
apply consistently to routing admission. The lookup revision also applies the
IP/subnet policy across referrals, retains configured late-reply records and
reserves one shared budget before sending routing requests. Completion, expiry,
failed sends and stopped searches return their reservations. Its periodic
scheduler rotates service order to avoid favoring one target when capacity is
constrained. Lookup consumption is implemented in the dependent lookup draft.

Example for a constrained connection (values are examples, not universal tuning
recommendations):

```ini
[Kad]
OutgoingRequests=256
LookupActiveGlobal=12
LookupPendingGlobal=128
LookupPendingPerSearch=24
LookupConcurrency=3
NodeConcurrency=1
RequestTimeoutMs=4000
LateReplyGraceMs=12000
```

The existing optional adaptive estimator may select a shorter interactive
request deadline, bounded by `RequestTimeoutMs`. Publication uses that configured
fixed timeout and quiet interval. Neither expiry nor a local capacity refusal
is evidence of peer misconduct.
