// SPDX-License-Identifier: GPL-2.0-or-later
// Parser stub exercises seek loops and finite forward seeks without media fixtures.
#include <cstdlib>
#include <cstdint>
#include <cwchar>
#include <cstddef>

#ifdef _WIN32
#define MI_CALL __stdcall
#else
#define MI_CALL
#endif

namespace
{
struct Parser
{
	unsigned calls = 0;
	bool full = false;
	uint64_t position = 0;
};
unsigned totalCalls = 0;
} // namespace

extern "C" {
void *MI_CALL MediaInfo_New()
{
	return new Parser;
}
void MI_CALL MediaInfo_Delete(void *h)
{
	delete static_cast<Parser *>(h);
}
size_t MI_CALL MediaInfo_Open_Buffer_Init(void *h, uint64_t, uint64_t offset)
{
	static_cast<Parser *>(h)->position = offset;
	return 0;
}
size_t MI_CALL MediaInfo_Open_Buffer_Continue(void *h, const unsigned char *, size_t size)
{
	// Terminate the old implementation promptly rather than waiting for its CPU limit.
	if (++totalCalls > (std::getenv("AMULE_TEST_FORWARD_SEEKS") ? 100U : 66U))
		std::exit(99);
	auto &parser = *static_cast<Parser *>(h);
	++parser.calls;
	parser.position += size;
	return 15; // Accepted, filled and finished, but a seek can still be pending.
}
uint64_t MI_CALL MediaInfo_Open_Buffer_Continue_GoTo_Get(void *h)
{
	const auto &parser = *static_cast<Parser *>(h);
	if (std::getenv("AMULE_TEST_FORWARD_SEEKS"))
		return parser.calls <= 80 ? parser.position + 1 : UINT64_MAX;
	// Retry mode spends 40 seeks on a codec-only first parse, then loops on the
	// full scan. A budget reset on that second parse trips the guard above.
	if (std::getenv("AMULE_TEST_SEEK_RETRY") && !parser.full && parser.calls > 40)
		return UINT64_MAX;
	return 0;
}
size_t MI_CALL MediaInfo_Open_Buffer_Finalize(void *)
{
	return 0;
}
const wchar_t *MI_CALL MediaInfo_Get(void *, int kind, size_t, const wchar_t *field, int, int)
{
	if (!std::wcscmp(field, L"Format"))
		return kind == 0 ? L"Wave" : L"AAC";
	if (!std::wcscmp(field, L"Duration") && std::getenv("AMULE_TEST_FORWARD_SEEKS"))
		return L"1800000";
	return L"";
}
size_t MI_CALL MediaInfo_Count_Get(void *, int kind, size_t)
{
	return kind == 2 ? 1 : 0;
}
const wchar_t *MI_CALL MediaInfo_Option(void *h, const wchar_t *field, const wchar_t *value)
{
	if (!std::wcscmp(field, L"ParseSpeed"))
		static_cast<Parser *>(h)->full = !std::wcscmp(value, L"1");
	return L"";
}
}
