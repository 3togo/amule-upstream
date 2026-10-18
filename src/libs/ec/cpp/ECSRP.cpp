// Copyright (c) 2026 aMule Team. GPL-2.0-or-later.
#include "ECSRP.h"

// Use the shared include prefix and scoped Crypto++ deprecation handling.
#define CRYPTOPP_INC_NEED_SRP
#include "../../../CryptoPP_Inc.h"

#include "ECCrypt.h"
#include "ECPacket.h"

#include <stdexcept>

namespace ECSRP
{
using CryptoPP::Integer;
namespace
{
struct Secret : Bytes
{
	using Bytes::Bytes;
	Secret(Bytes bytes)
	: Bytes(std::move(bytes))
	{
	}
	~Secret() { ECCrypt::SecureWipe(*this); }
};
Integer Number(const Bytes &b)
{
	return Integer(b.data(), b.size());
}
Bytes Encode(const Integer &n, size_t width)
{
	Bytes b(width);
	n.Encode(b.data(), b.size());
	return b;
}
Bytes Join(Bytes a, const Bytes &b)
{
	a.insert(a.end(), b.begin(), b.end());
	return a;
}
Bytes Hash(const Bytes &b, bool sha1 = false)
{
	Bytes out(sha1 ? 20 : 32);
	if (sha1) {
		CryptoPP::SHA1().CalculateDigest(out.data(), b.data(), b.size());
	} else {
		CryptoPP::SHA256().CalculateDigest(out.data(), b.data(), b.size());
	}
	return out;
}
Integer Prime(const Arithmetic &g)
{
	return Integer((g.modulus + "h").c_str());
}
Integer Scrambling(const Arithmetic &g, const Bytes &A, const Bytes &B)
{
	const Integer n = Prime(g);
	if (A.size() != n.ByteCount() || B.size() != n.ByteCount() || Number(A) <= Integer::Zero() ||
		Number(A) >= n || Number(B) <= Integer::Zero() || Number(B) >= n) {
		throw std::invalid_argument("Invalid SRP public value");
	}
	const Integer u = Number(Hash(Join(A, B), g.sha1));
	if (u.IsZero()) {
		throw std::invalid_argument("Invalid SRP scrambling value");
	}
	return u;
}
Integer Generator(const Arithmetic &g)
{
	return Integer(g.generator);
}
Integer Multiplier(const Arithmetic &g)
{
	const Integer n = Prime(g);
	return Number(Hash(Join(Encode(n, n.ByteCount()), Encode(Generator(g), n.ByteCount())), g.sha1));
}
Bytes Derive(const std::string &digest, const Bytes &salt)
{
	if (salt.size() != 16) {
		throw std::invalid_argument("Invalid SRP salt");
	}
	Secret input(Unhex(digest, 16));
	Bytes out(32);
	CryptoPP::Scrypt().DeriveKey(
		out.data(), out.size(), input.data(), input.size(), salt.data(), salt.size(), 32768, 8, 1);
	return out;
}
Bytes Context(uint8_t credential, const Bytes &salt, const Bytes &transcript, const Bytes &A, const Bytes &B)
{
	if (credential > 1 || salt.size() != 16 || transcript.empty()) {
		throw std::invalid_argument("Invalid SRP context");
	}
	// Fixed v1 profile; transcript length is uint32, network byte order.
	Bytes out{ 1, credential, 15, 8, 1 };
	out = Join(out, salt);
	const uint32_t len = transcript.size();
	for (int shift = 24; shift >= 0; shift -= 8) {
		out.push_back(static_cast<uint8_t>(len >> shift));
	}
	return Join(Join(Join(out, transcript), A), B);
}
Bytes Proof(const Bytes &key, const char *label, const Bytes &context, const Bytes &m1 = {})
{
	CryptoPP::HMAC<CryptoPP::SHA256> hmac(key.data(), key.size());
	hmac.Update(reinterpret_cast<const uint8_t *>(label), std::char_traits<char>::length(label));
	hmac.Update(context.data(), context.size());
	hmac.Update(m1.data(), m1.size());
	Bytes out(32);
	hmac.Final(out.data());
	return out;
}
} // namespace

Bytes Field(const CECPacket &packet, uint16_t name, size_t size)
{
	const CECTag *found = nullptr;
	for (const auto &tag : packet) {
		if (tag.GetTagName() == name) {
			if (found)
				throw std::invalid_argument("Duplicate SRP field");
			found = &tag;
		}
	}
	if (!found || !found->IsCustom() || found->GetTagDataLen() != size || found->GetTagCount() != 0)
		throw std::invalid_argument("Missing or malformed SRP field");
	const auto *data = static_cast<const uint8_t *>(found->GetTagData());
	return Bytes(data, data + size);
}
Bytes PasswordInput(const std::string &digest, const Bytes &salt)
{
	return Derive(digest, salt);
}
Proofs Evidence(const Bytes &shared,
	uint8_t credential,
	const Bytes &salt,
	const Bytes &transcript,
	const Bytes &A,
	const Bytes &B)
{
	if (shared.size() != WIDTH)
		throw std::invalid_argument("Invalid SRP secret width");
	const auto context = Context(credential, salt, transcript, A, B);
	Secret k(Hash(shared));
	const auto m1 = Proof(k, "ec-srp-client", context);
	return { m1, Proof(k, "ec-srp-server", context, m1) };
}
const char *Profile()
{
	return "v1:15:8:1:";
}
Bytes Unhex(const std::string &text, size_t size)
{
	if (text.size() != size * 2) {
		throw std::invalid_argument("Invalid SRP hex length");
	}
	auto nibble = [](char c) -> uint8_t {
		if (c >= '0' && c <= '9')
			return c - '0';
		if (c >= 'a' && c <= 'f')
			return c - 'a' + 10;
		if (c >= 'A' && c <= 'F')
			return c - 'A' + 10;
		throw std::invalid_argument("Invalid SRP hex");
	};
	Bytes out(size);
	for (size_t i = 0; i < size; ++i)
		out[i] = (nibble(text[2 * i]) << 4) | nibble(text[2 * i + 1]);
	return out;
}
std::string Hex(const Bytes &b)
{
	std::string out;
	for (auto c : b) {
		out += "0123456789abcdef"[c >> 4];
		out += "0123456789abcdef"[c & 15];
	}
	return out;
}
Arithmetic::Arithmetic()
: modulus("FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD129024E08"
	  "8A67CC74020BBEA63B139B22514A08798E3404DDEF9519B3CD3A431B"
	  "302B0A6DF25F14374FE1356D6D51C245E485B576625E7EC6F44C42E9"
	  "A637ED6B0BFF5CB6F406B7EDEE386BFB5A899FA5AE9F24117C4B1FE6"
	  "49286651ECE45B3DC2007CB8A163BF0598DA48361C55D39A69163FA8F"
	  "D24CF5F83655D23DCA3AD961C62F356208552BB9ED529077096966D670"
	  "C354E4ABC9804F1746C08CA18217C32905E462E36CE3BE39E772C180E"
	  "86039B2783A2EC07A28FB5C55DF06F4C52C9DE2BCBF6955817183995"
	  "497CEA956AE515D2261898FA051015728E5A8AAAC42DAD33170D04507A"
	  "33A85521ABDF1CBA64ECFB850458DBEF0A8AEA71575D060C7DB3970F8"
	  "5A6E1E4C7ABF5AE8CDB0933D71E8C94E04A25619DCEE3D2261AD2EE"
	  "6BF12FFA06D98A0864D87602733EC86A64521F2B18177B200CBBE1175"
	  "77A615D6C770988C0BAD946E208E24FA074E5AB3143DB5BFCE0FD108E"
	  "4B82D120A93AD2CAFFFFFFFFFFFFFFFF")
{
}
Bytes Arithmetic::Public(const Bytes &exponent) const
{
	const Integer n = Prime(*this);
	return Encode(a_exp_b_mod_c(Generator(*this), Number(exponent), n), n.ByteCount());
}
Bytes Arithmetic::ServerPublic(const Bytes &v, const Bytes &b) const
{
	const Integer n = Prime(*this);
	return Encode((Multiplier(*this) * Number(v) + Number(Public(b))) % n, n.ByteCount());
}
Bytes Arithmetic::ClientSecret(const Bytes &A, const Bytes &B, const Bytes &a, const Bytes &x) const
{
	const Integer n = Prime(*this), u = Scrambling(*this, A, B);
	const Integer base = (Number(B) - Multiplier(*this) * Number(Public(x))) % n;
	return Encode(a_exp_b_mod_c(base, Number(a) + u * Number(x), n), n.ByteCount());
}
Bytes Arithmetic::ServerSecret(const Bytes &A, const Bytes &B, const Bytes &b, const Bytes &v) const
{
	const Integer n = Prime(*this), u = Scrambling(*this, A, B);
	const Integer base = (Number(A) * a_exp_b_mod_c(Number(v), u, n)) % n;
	return Encode(a_exp_b_mod_c(base, Number(b), n), n.ByteCount());
}
bool Verifier::Valid() const
{
	return salt.size() == 16 && value.size() == WIDTH && Number(value) > Integer::One() &&
	       Number(value) < Prime(Arithmetic());
}
std::string Verifier::Encode() const
{
	if (!Valid())
		throw std::invalid_argument("Invalid SRP verifier");
	return std::string(Profile()) + Hex(salt) + ":" + Hex(value);
}
Verifier Verifier::Decode(const std::string &text)
{
	const std::string prefix(Profile());
	if (text.size() != prefix.size() + 32 + 1 + 2 * WIDTH || text.compare(0, prefix.size(), prefix) ||
		text[prefix.size() + 32] != ':') {
		throw std::invalid_argument("Invalid or unsupported ECPasswordVerifier profile");
	}
	Verifier v{ Unhex(text.substr(prefix.size(), 32), 16),
		Unhex(text.substr(prefix.size() + 33), WIDTH) };
	if (!v.Valid())
		throw std::invalid_argument("Invalid ECPasswordVerifier value");
	return v;
}
Verifier Verifier::Create(const std::string &digest)
{
	Verifier v;
	v.salt = ECCrypt::RandomBytes(16);
	Secret x(Derive(digest, v.salt));
	v.value = Arithmetic().Public(x);
	if (!v.Valid())
		throw std::runtime_error("Could not generate EC verifier");
	return v;
}
Client::Client() = default;
Client::~Client()
{
	Reset();
}
void Client::Reset()
{
	ECCrypt::SecureWipe(m_a);
	m_A.clear();
	ECCrypt::SecureWipe(m_expected);
}
Bytes Client::Start()
{
	Reset();
	m_a = ECCrypt::RandomBytes(32);
	if (m_a.size() != 32 || Number(m_a).IsZero())
		throw std::runtime_error("No SRP randomness");
	m_A = Arithmetic().Public(m_a);
	return m_A;
}
Bytes Client::Respond(const std::string &digest,
	uint8_t credential,
	const Bytes &salt,
	const Bytes &B,
	const Bytes &transcript)
{
	if (m_a.empty())
		throw std::logic_error("SRP client not started");
	// Validate public values and context before the expensive KDF.
	const Arithmetic g;
	Scrambling(g, m_A, B);
	(void)Context(credential, salt, transcript, m_A, B);
	Secret x(Derive(digest, salt));
	Secret s(g.ClientSecret(m_A, B, m_a, x));
	const auto proofs = Evidence(s, credential, salt, transcript, m_A, B);
	const auto &proof = proofs.client;
	m_expected = proofs.server;
	ECCrypt::SecureWipe(m_a);
	return proof;
}
bool Client::Verify(const Bytes &proof)
{
	const bool ok = !m_expected.empty() && ECCrypt::ConstantTimeEquals(proof, m_expected);
	Reset();
	return ok;
}
Server::~Server()
{
	Reset();
}
void Server::Reset()
{
	ECCrypt::SecureWipe(m_expected);
	ECCrypt::SecureWipe(m_proof);
}
Bytes Server::Start(const Verifier &v, uint8_t credential, const Bytes &A, const Bytes &transcript)
{
	Reset();
	if (!v.Valid())
		throw std::invalid_argument("Invalid SRP verifier");
	Secret b(ECCrypt::RandomBytes(32));
	if (b.size() != 32 || Number(b).IsZero())
		throw std::runtime_error("No SRP randomness");
	const Arithmetic g;
	const auto B = g.ServerPublic(v.value, b);
	(void)Context(credential, v.salt, transcript, A, B);
	Secret s(g.ServerSecret(A, B, b, v.value));
	const auto proofs = Evidence(s, credential, v.salt, transcript, A, B);
	m_expected = proofs.client;
	m_proof = proofs.server;
	return B;
}
Bytes Server::Verify(const Bytes &proof)
{
	Bytes response;
	if (!m_expected.empty() && ECCrypt::ConstantTimeEquals(proof, m_expected))
		response = m_proof;
	Reset();
	return response;
}
} // namespace ECSRP
