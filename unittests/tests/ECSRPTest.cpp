// Copyright (c) 2026 aMule Team. GPL-2.0-or-later.
#include "ECSRP.h"
#include "ECSRPVector.h"
#include "ECSRPConfig.h"
#include "ECClientPassword.h"
#include <wx/fileconf.h>
#include "ECCrypt.h"
#include "ECPacket.h"
#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace ECSRP;
static void Check(bool ok, const char *message)
{
	if (!ok)
		throw std::runtime_error(message);
}
template <typename F> static void Reject(F f)
{
	try {
		f();
	} catch (const std::exception &) {
		return;
	}
	throw std::runtime_error("Malformed input accepted");
}
static Bytes H(const std::string &s)
{
	return Unhex(s, s.size() / 2);
}

class FaultConfig : public wxFileConfig
{
public:
	FaultConfig()
	: wxFileConfig("", "", "", "", 0)
	{
	}
	int calls = 0, failAt = 0;
	wxString savedVerifier, savedLegacy;
	bool savedHasVerifier = false;
	bool Flush(bool = false) override
	{
		if (++calls == failAt)
			return false;
		savedHasVerifier = HasEntry("/ExternalConnect/ECPasswordVerifier");
		savedVerifier = Read("/ExternalConnect/ECPasswordVerifier", wxString());
		savedLegacy = Read("/ExternalConnect/ECPassword", wxString());
		return true;
	}
};

int main()
{
	try {
		// Confirmed remote rotations update reconnect state while respecting the
		// user's save-password choice and credentials saved for other endpoints.
		for (int scenario = 0; scenario < 6; ++scenario) {
			FaultConfig cfg;
			cfg.Write("/EC/Host", scenario == 1 ? "other-host" : "localhost");
			cfg.Write("/EC/Port", scenario == 2 ? 4713L : 4712L);
			if (scenario != 3)
				cfg.Write("/EC/Password", scenario == 5 ? "unrelated" : "old");
			if (scenario == 4)
				cfg.failAt = 1;
			wxString session = "old";
			Check(ECClientPassword::Commit(cfg, "localhost", 4712, session, "new") ==
					(scenario != 4),
				"saved client credential failure was hidden");
			Check(session == "new", "reconnect kept the old password after rotation");
			Check(cfg.calls == ((scenario == 0 || scenario == 4) ? 1 : 0),
				"rotation changed an unrelated or unsaved credential");
			if (scenario == 1 || scenario == 2)
				Check(cfg.Read("/EC/Password", wxString()) == "old",
					"rotation overwrote another endpoint's password");
			if (scenario == 3)
				Check(!cfg.HasEntry("/EC/Password"), "rotation enabled password saving");
			if (scenario == 0)
				Check(cfg.Read("/EC/Password", wxString()) == "new",
					"remembered password was not updated");
			if (scenario == 5)
				Check(cfg.Read("/EC/Password", wxString()) == "unrelated",
					"rotation overwrote a separately saved credential");
		}
		// RFC 5054 Appendix B. These exercise the same arithmetic as production,
		// with the RFC's SHA-1 and 1024-bit group instead of EC's v1 profile.
		Arithmetic rfc;
		rfc.generator = 2;
		rfc.sha1 = true;
		rfc.modulus = "EEAF0AB9ADB38DD69C33F80AFA8FC5E86072618775FF3C0B9EA2314C9C256576"
			      "D674DF7496EA81D3383B4813D692C6E0E0D5D8E250B98BE48E495C1D6089DAD1"
			      "5DC7D7B46154D6B6CE8EF4AD69B15D4982559B297BCF1885C529F566660E57EC"
			      "68EDBC3C05726CC02FD4CBF4976EAA9AFD5138FE8376435B9FC61D2FC0EB06E3";
		const auto x = H("94B7555AABE9127CC58CCF4993DB6CF84D16C124");
		const auto a = H("60975527035CF2AD1989806F0407210BC81EDC04E2762A56AFD529DDDA2D4393");
		const auto b = H("E487CB59D31AC550471E81F00F6928E01DDA08E974A004F49E61F5D105284D20");
		const auto v = H("7E273DE8696FFC4F4E337D05B4B375BEB0DDE1569E8FA00A9886D812"
				 "9BADA1F1822223CA1A605B530E379BA4729FDC59F105B4787E5186F5"
				 "C671085A1447B52A48CF1970B4FB6F8400BBF4CEBFBB168152E08AB5"
				 "EA53D15C1AFF87B2B9DA6E04E058AD51CC72BFC9033B564E26480D78"
				 "E955A5E29E7AB245DB2BE315E2099AFB");
		const auto A = H("61D5E490F6F1B79547B0704C436F523DD0E560F0C64115BB72557EC4"
				 "4352E8903211C04692272D8B2D1A5358A2CF1B6E0BFCF99F921530EC"
				 "8E39356179EAE45E42BA92AEACED825171E1E8B9AF6D9C03E1327F44"
				 "BE087EF06530E69F66615261EEF54073CA11CF5858F0EDFDFE15EFEA"
				 "B349EF5D76988A3672FAC47B0769447B");
		const auto B = H("BD0C61512C692C0CB6D041FA01BB152D4916A1E77AF46AE105393011"
				 "BAF38964DC46A0670DD125B95A981652236F99D9B681CBF87837EC99"
				 "6C6DA04453728610D0C6DDB58B318885D7D82C7F8DEB75CE7BD4FBAA"
				 "37089E6F9C6059F388838E7A00030B331EB76840910440B1B27AAEAE"
				 "EB4012B7D7665238A8E3FB004B117B58");
		const auto S = H("B0DC82BABCF30674AE450C0287745E7990A3381F63B387AAF271A10D"
				 "233861E359B48220F7C4693C9AE12B0A6F67809F0876E2D013800D6C"
				 "41BB59B6D5979B5C00A172B4A2A5903A0BDCAF8A709585EB2AFAFA8F"
				 "3499B200210DCC1F10EB33943CD67FC88A2F39A4BE5BEC4EC0A3212D"
				 "C346D7E474B29EDE8A469FFECA686E5A");
		Check(rfc.Public(x) == v, "RFC verifier");
		Check(rfc.Public(a) == A, "RFC A");
		auto sha256Group = rfc;
		sha256Group.sha1 = false;
		Check(sha256Group.Public(a) == A, "changing the hash changed the group generator");
		Check(rfc.ServerPublic(v, b) == B, "RFC B");
		Check(rfc.ClientSecret(A, B, a, x) == S, "RFC client S");
		Check(rfc.ServerSecret(A, B, b, v) == S, "RFC server S");
		Reject([&] { rfc.ClientSecret(A, Bytes(128), a, x); });
		Reject([&] { rfc.ServerSecret(Bytes(128), B, b, v); });

		// Fixed vectors generated once with Python's independent primitives.
		Bytes vectorSalt(16), vectorA(32), vectorB(32), vectorTranscript(163);
		for (size_t i = 0; i < 16; ++i)
			vectorSalt[i] = i;
		for (size_t i = 0; i < 32; ++i) {
			vectorA[i] = i;
			vectorB[i] = i + 32;
		}
		for (size_t i = 0; i < 163; ++i)
			vectorTranscript[i] = i;
		const auto vectorX = PasswordInput("0123456789abcdef0123456789abcdef", vectorSalt);
		const Arithmetic production;
		Check(vectorX == H(V1Vector::x), "v1 scrypt input");
		Check(production.Public(vectorX) == H(V1Vector::v), "v1 verifier");
		Check(production.Public(vectorA) == H(V1Vector::A), "v1 A");
		Check(production.ServerPublic(H(V1Vector::v), vectorB) == H(V1Vector::B), "v1 B");
		Check(production.ClientSecret(H(V1Vector::A), H(V1Vector::B), vectorA, vectorX) ==
				H(V1Vector::S),
			"v1 client S");
		Check(production.ServerSecret(H(V1Vector::A), H(V1Vector::B), vectorB, H(V1Vector::v)) ==
				H(V1Vector::S),
			"v1 server S");
		const auto evidence = Evidence(
			H(V1Vector::S), 0, vectorSalt, vectorTranscript, H(V1Vector::A), H(V1Vector::B));
		Check(evidence.client == H(V1Vector::m1), "v1 M1");
		Check(evidence.server == H(V1Vector::m2), "v1 M2");

		const std::string password = "0123456789abcdef0123456789abcdef";
		const auto before = std::chrono::steady_clock::now();
		const auto verifier = Verifier::Create(password);
		std::cout << "v1 verifier generation ms="
			  << std::chrono::duration_cast<std::chrono::milliseconds>(
				     std::chrono::steady_clock::now() - before)
				     .count()
			  << '\n';
		Check(verifier.value.size() == WIDTH, "3072-bit verifier width");
		for (int failAt : { 0, 1, 2 }) {
			FaultConfig cfg;
			cfg.Write("/ExternalConnect/ECPassword", wxString::FromUTF8(password.c_str()));
			cfg.failAt = failAt;
			wxString record, error;
			const bool loaded = LoadCredential(cfg, record, error);
			Check(loaded == (failAt != 1), "migration commit result");
			if (failAt == 1) {
				Check(record.IsEmpty() &&
						!cfg.HasEntry("/ExternalConnect/ECPasswordVerifier"),
					"failed migration changed verifier");
				Check(cfg.Read("/ExternalConnect/ECPassword", wxString()) == password,
					"failed migration lost old credential");
			} else {
				Check(Verifier::Decode(std::string(record.utf8_str())).Valid(),
					"persisted invalid verifier");
				Check(!cfg.savedVerifier.IsEmpty(), "cleanup before verifier commit");
				FaultConfig restarted;
				restarted.Write("/ExternalConnect/ECPasswordVerifier", cfg.savedVerifier);
				if (!cfg.savedLegacy.IsEmpty())
					restarted.Write("/ExternalConnect/ECPassword", cfg.savedLegacy);
				wxString again;
				Check(LoadCredential(restarted, again, error), "migration restart failed");
				Check(again == record, "migration regenerated verifier");
				Check(!restarted.HasEntry("/ExternalConnect/ECPassword"),
					"restart did not clean legacy key");
			}
		}
		FaultConfig corrupt;
		corrupt.Write("/ExternalConnect/ECPassword", wxString::FromUTF8(password.c_str()));
		corrupt.Write("/ExternalConnect/ECPasswordVerifier", "v9:unsupported");
		wxString bad, error;
		Check(!LoadCredential(corrupt, bad, error),
			"malformed verifier fell back to legacy credential");
		Check(corrupt.calls == 0 && corrupt.HasEntry("/ExternalConnect/ECPassword"),
			"malformed verifier modified config");
		FaultConfig rekey;
		rekey.Write(
			"/ExternalConnect/ECPasswordVerifier", wxString::FromUTF8(verifier.Encode().c_str()));
		wxString existing = wxString::FromUTF8(verifier.Encode().c_str());
		rekey.failAt = 1;
		Check(!StoreCredential(rekey, "abcdef0123456789abcdef0123456789", existing, error),
			"failed rekey accepted");
		Check(std::string(existing.utf8_str()) == verifier.Encode(),
			"failed rekey changed live credential");
		Check(rekey.Read("/ExternalConnect/ECPasswordVerifier", wxString()) == existing,
			"failed rekey changed config");
		Check(Verifier::Decode(verifier.Encode()).value == verifier.value, "verifier round trip");
		Reject([&] { Verifier::Decode("v1:31:8:1:" + verifier.Encode().substr(10)); });
		Reject([&] { Verifier::Decode(verifier.Encode() + "x"); });
		Reject([&] { Verifier::Decode("v2" + verifier.Encode().substr(2)); });
		Reject([&] { Unhex(std::string(32, 'z'), 16); });
		Client client;
		Server server;
		const Bytes transcript(163, 42);
		for (uint8_t credential : { 0, 1 }) {
			const auto cA = client.Start();
			const auto sB = server.Start(verifier, credential, cA, transcript);
			const auto m1 = client.Respond(password, credential, verifier.salt, sB, transcript);
			const auto m2 = server.Verify(m1);
			Check(client.Verify(m2), "full proof handshake");
			Check(!client.Verify(m2), "server proof replay rejected");
			Check(server.Verify(m1).empty(), "client proof replay rejected");
		}
		{
			const auto cA = client.Start();
			const auto sB = server.Start(verifier, 0, cA, transcript);
			const auto m1 = client.Respond(password, 0, verifier.salt, sB, transcript);
			auto m2 = server.Verify(m1);
			m2[0] ^= 1;
			Check(!client.Verify(m2), "tampered server proof accepted");
			Reject([&] { client.Respond(password, 0, verifier.salt, sB, transcript); });
		}
		{
			const auto cA = client.Start();
			const auto sB = server.Start(verifier, 0, cA, transcript);
			Check(server.Verify(client.Respond(password, 1, verifier.salt, sB, transcript))
					.empty(),
				"credential selector not bound");
		}
		// Every field of the X25519 transcript is bound, not just the SRP A/B.
		for (size_t i : { size_t(0),
			     size_t(1),
			     size_t(2),
			     size_t(3),
			     size_t(35),
			     size_t(67),
			     size_t(99),
			     size_t(131),
			     size_t(162) }) {
			auto changed = transcript;
			changed[i] ^= 1;
			const auto cA = client.Start();
			const auto sB = server.Start(verifier, 0, cA, transcript);
			Check(server.Verify(client.Respond(password, 0, verifier.salt, sB, changed)).empty(),
				"transcript mismatch accepted");
		}
		// A stolen verifier is not a client credential; using its first 16 bytes
		// as the digest (or sending it directly as M1) must fail.
		for (const auto &guess : { std::string(32, '0'),
			     Hex(Bytes(verifier.value.begin(), verifier.value.begin() + 16)) }) {
			const auto cA = client.Start();
			const auto sB = server.Start(verifier, 0, cA, transcript);
			Check(server.Verify(client.Respond(guess, 0, verifier.salt, sB, transcript)).empty(),
				"wrong/stolen credential accepted");
		}
		const auto cA = client.Start();
		const auto sB = server.Start(verifier, 0, cA, transcript);
		Check(server.Verify(verifier.value).empty(), "verifier accepted as proof");
		Reject([&] { client.Respond(password, 0, verifier.salt, Bytes(WIDTH), transcript); });
		CECPacket packet(EC_OP_AUTH_REQ);
		packet.AddTag(CECTag(EC_TAG_SRP_A, cA.size(), cA.data()));
		Check(Field(packet, EC_TAG_SRP_A, WIDTH) == cA, "field parse");
		packet.AddTag(CECTag(EC_TAG_SRP_A, cA.size(), cA.data()));
		Reject([&] { Field(packet, EC_TAG_SRP_A, WIDTH); });
		CECPacket typed(EC_OP_AUTH_REQ);
		typed.AddTag(CECTag(EC_TAG_SRP_A, uint8_t(3)));
		Reject([&] { Field(typed, EC_TAG_SRP_A, WIDTH); });
		std::cout << "ECSRP tests passed\n";
	} catch (const std::exception &e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
