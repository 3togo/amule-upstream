// Copyright (c) 2026 aMule Team. GPL-2.0-or-later.
#ifndef ECSRP_H
#define ECSRP_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class CECPacket;

namespace ECSRP
{
using Bytes = std::vector<uint8_t>;
constexpr size_t WIDTH = 384;
// v1 deliberately supports one bounded profile. Never allocate using peer costs.
const char *Profile();
struct Verifier
{
	Bytes salt;
	Bytes value;
	std::string Encode() const;
	static Verifier Decode(const std::string &text);
	static Verifier Create(const std::string &digest);
	bool Valid() const;
};

// Arithmetic is parameterized for the RFC 5054 Appendix B SHA-1/1024-bit test.
// Production callers use only the fixed v1 constructor defaults.
struct Arithmetic
{
	std::string modulus;
	bool sha1 = false;
	Arithmetic();
	Bytes Public(const Bytes &exponent) const;
	Bytes ServerPublic(const Bytes &verifier, const Bytes &b) const;
	Bytes ClientSecret(const Bytes &A, const Bytes &B, const Bytes &a, const Bytes &x) const;
	Bytes ServerSecret(const Bytes &A, const Bytes &B, const Bytes &b, const Bytes &v) const;
};

class Client
{
public:
	Client();
	~Client();
	Client(const Client &) = delete;
	Client &operator=(const Client &) = delete;
	Bytes Start();
	Bytes Respond(const std::string &digest,
		uint8_t credential,
		const Bytes &salt,
		const Bytes &B,
		const Bytes &transcript);
	bool Verify(const Bytes &proof);
	void Reset();

private:
	Bytes m_a, m_A, m_expected;
};

class Server
{
public:
	Server() = default;
	~Server();
	Server(const Server &) = delete;
	Server &operator=(const Server &) = delete;
	Bytes Start(const Verifier &verifier, uint8_t credential, const Bytes &A, const Bytes &transcript);
	Bytes Verify(const Bytes &proof);
	void Reset();

private:
	Bytes m_expected, m_proof;
};

Bytes Field(const CECPacket &packet, uint16_t name, size_t size);
Bytes PasswordInput(const std::string &digest, const Bytes &salt);
struct Proofs
{
	Bytes client, server;
};
Proofs Evidence(const Bytes &shared,
	uint8_t credential,
	const Bytes &salt,
	const Bytes &transcript,
	const Bytes &A,
	const Bytes &B);
// Strict, fixed-size decoding; no secret material is returned on malformed input.
Bytes Unhex(const std::string &text, size_t size);
std::string Hex(const Bytes &bytes);
} // namespace ECSRP
#endif
