# EC authentication, verifier format v1

Upgrade the core and every EC client together to 3.2.0 or later. Authentication
requires both SRP-6a and the encrypted X25519 channel, including on loopback.
There is no legacy password challenge or plaintext fallback. The removed
`--disable-ec-encryption` option is an unknown option. Old `Encryption` and
`RequireEncryption` config keys are ignored. An old client receives an error
requiring a 3.2.0 client; a new client facing an old daemon requires a 3.2.0 daemon.

## Exact construction

`N_srp` is the RFC 5054 Appendix A 3072-bit prime, `g = 5`, and `H = SHA256`.
All SRP integers are unsigned and big endian. `PAD` is exactly 384 bytes.
Wire A, B and stored v must have that exact width and be strictly between zero
and `N_srp` (v must also exceed one). Noncanonical encodings are refused.
Both sides reject `u = 0`; each attempt uses fresh random 256-bit a and b.
No group or hash is selected by the peer.

```
P = decode_hex(MD5_hex(password))       # exactly 16 raw bytes, not hex text
x = integer(scrypt(P, salt, N_scrypt=32768, r=8, p=1, dkLen=32))
v = g^x mod N_srp
k = integer(H(PAD(N_srp) || PAD(g)))
A = g^a mod N_srp
B = (k*v + g^b) mod N_srp
u = integer(H(PAD(A) || PAD(B)))
S_client = ((B - k*g^x) mod N_srp)^(a + u*x) mod N_srp
S_server = (A * v^u mod N_srp)^b mod N_srp
K = H(PAD(S))
```

The salt is 16 random bytes. v1 supports only the fixed scrypt profile above;
clients check the version/profile and all fixed field lengths before running
the KDF. Unsupported costs are rejected, never allocated or silently reduced.
The main ROMix array needs 32 MiB plus implementation overhead. The cost is
paid once per client login and when generating a verifier, not per command on
an established connection. `x` is wiped after constructing the client proof;
there is no process cache or disk cache. Separate amulecmd invocations each
perform their own login. Slower platforms still require performance validation.

The stored `/ExternalConnect/ECPasswordVerifier` is exactly:

```
v1:15:8:1:<32 hex characters of salt>:<768 hex characters of v>
```

Lowercase hex is emitted; either case is accepted. Unknown versions, extra
fields, invalid hex, invalid v or other costs fail closed. Neither x nor the
MD5 digest is stored on the daemon. Client config passwords remain credentials.

## Four messages

The existing X25519 transcript T and session key derivation are defined in
[EC_Protocol.md §1.3](EC_Protocol.md#section-13--transport-encryption).
SRP does not replace or contribute to the X25519 transport keys.

1. Plain `EC_OP_AUTH_REQ`: existing client/protocol/capability fields, cipher
   offer, client nonce/public key, plus custom `EC_TAG_SRP_A` (384 bytes) and
   `EC_TAG_SRP_CREDENTIAL` (one byte: 0=password, 1=spawned API token).
2. Plain `EC_OP_AUTH_SALT`: server cipher/nonce/public key plus custom
   `EC_TAG_SRP_B` (384 bytes), `EC_TAG_SRP_SALT` (16 bytes),
   `EC_TAG_SRP_PROFILE` (four bytes: `01 0f 08 01`). Both sides now install
   the transport keys. The daemon starts sealing after sending this message.
3. Sealed `EC_OP_AUTH_PASSWD`: custom `EC_TAG_SRP_M1` (32 bytes).
4. Sealed `EC_OP_AUTH_OK`: custom `EC_TAG_SRP_M2` (32 bytes), server version,
   and accepted capability tags. No command is sent or accepted before proof
   verification. An authentication failure terminates the handshake; after
   the salt it is also sealed. Replayed or out-of-order messages cannot resume it.

SRP fields must be unique custom tags without children. Define the proof context
with no terminators and with `len(T)` encoded as a four-byte network-order uint32:

```
C = 01 || credential_byte || 0f || 08 || 01 || salt || len(T) || T || PAD(A) || PAD(B)
M1 = HMAC-SHA256(K, ASCII("ec-srp-client") || C)
M2 = HMAC-SHA256(K, ASCII("ec-srp-server") || C || M1)
```

Proofs use constant-time comparison. The legacy salt tag, MD5 response and
password-keyed AEAD confirmation tags are retired; their tag IDs are not reused.

## Migration and credential changes

When the verifier key is absent, the core derives a verifier from the existing
ECPassword digest, writes and flushes it, then deletes and flushes the old key.
wxFileConfig writes through a temporary file. Failure of the first commit
preserves the old credential and does not enable authentication with an
unpersisted verifier. Interrupted cleanup is retried on restart, using the
already committed verifier and salt. A present malformed/unsupported verifier
never falls back to ECPassword. CLI, local preferences and authenticated remote
preferences generate a fresh salt and verifier for a changed password.
The local dialog commits the password only after validation and cancellable
work. Remote preference snapshots never consume or retain a pending EC digest;
the edit is sent explicitly at commit and cleared only on acknowledgement.
Confirmed changes update the GUI's reconnect credential and, only if the active
credential was already saved for that endpoint, its saved client password.
Failed requests preserve the pending edit for retry; cancelling discards it.

Config backups and saved client digests are not revoked by deleting the live
key. A downgrade has no ECPassword and requires setting a password again with
the older core. Do not copy a verifier into ECPassword.

The core builds an in-memory verifier when issuing a child API token, and
revokes it with the token. The token is decoded directly as 16 raw bytes on
both ends; it is not hashed again. A random dummy token verifier is prepared
before accepting connections. Missing tokens take the same login path, with
the same profile, sizes and generic wrong-password result. Before accepting M1 the core
checks that the selected verifier is still current, so password changes and
token revocation also invalidate pending handshakes. No per-request
scrypt or dummy-verifier generation exposes token availability.

## Security scope and validation

A stolen verifier permits offline password guesses and server impersonation.
It does not itself permit client impersonation. An active intermediary without
the verifier that establishes separate X25519 exchanges cannot relay the bound
SRP proofs successfully. SRP does not protect a compromised endpoint. Crypto++
integer exponentiation is not claimed to be constant-time; local side-channel
resistance needs separate review. These tests are not a formal proof of this
EC-specific composition.

`ECSRPTest` checks the RFC 5054 Appendix B arithmetic separately from fixed v1
vectors for SHA256, scrypt, padded S and EC evidence. Regenerate the latter with
`GenerateECSRPVector.py`, an independent Python implementation. Integration
fixtures in `ECWire.py` use Python/cryptography rather than Crypto++, exercising
the production daemon and clients. Fault-injected config tests cover failed
commits, interrupted cleanup, restart and malformed existing verifiers.
The asynchronous GUI transport is also queried for connection locality during
login to verify that UI queries do not change SRP state. Both client transports
reject missing or tampered server evidence before sending commands.
`ECSRPApiIntegrationTest` runs the production amuleapi against an offline core
with both a configured password and a spawned token, including an obsolete
`Encryption=0` setting. The token case uses a wrong configured password to rule
out fallback and checks that the token file was consumed.
