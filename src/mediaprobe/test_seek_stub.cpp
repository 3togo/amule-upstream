// SPDX-License-Identifier: GPL-2.0-or-later
// A hostile parser stub exercises the real helper's seek budget without media fixtures.
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
size_t MI_CALL MediaInfo_Open_Buffer_Init(void *, uint64_t, uint64_t)
{
	return 0;
}
size_t MI_CALL MediaInfo_Open_Buffer_Continue(void *h, const unsigned char *, size_t)
{
	// Terminate the old implementation promptly rather than waiting for its CPU limit.
	if (++totalCalls > 66)
		std::exit(99);
	++static_cast<Parser *>(h)->calls;
	return 15; // Accepted, filled and finished, but a seek can still be pending.
}
uint64_t MI_CALL MediaInfo_Open_Buffer_Continue_GoTo_Get(void *h)
{
	const auto &parser = *static_cast<Parser *>(h);
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
