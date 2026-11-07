// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstdio>
#include "mediaprobe/sandbox.h"
int main()
{
	std::fputs("controlled confinement setup failure (validation fixture)\n", stderr);
	return probe::sandbox::kSetupFailed;
}
