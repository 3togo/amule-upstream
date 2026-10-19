//
// This file is part of the aMule Project.
//
// Copyright (c) 2003-2026 aMule Team ( https://amule-org.github.io )
//
// Any parts of this program derived from the xMule, lMule or eMule project,
// or contributed by third-party developers are copyrighted by their
// respective authors.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA
//

#include <muleunit/test.h>
#include "WindowGeometry.h"
#include <limits>

using namespace muleunit;
DECLARE_SIMPLE(WindowGeometry)

TEST(WindowGeometry, RemovedMonitorAndNegativeCoordinatesRecover)
{
	const std::vector<wxRect> areas{ wxRect(0, 24, 1920, 1056) };
	const wxRect result = ConstrainWindowRect(wxRect(-5000, -4000, 800, 600), areas, wxSize(320, 240));
	ASSERT_TRUE(areas[0].Contains(result));
	ASSERT_TRUE((wxSize(800, 600)) == (result.GetSize()));
}
TEST(WindowGeometry, ValidNegativeMonitorIsPreserved)
{
	const std::vector<wxRect> areas{ wxRect(0, 0, 1920, 1080), wxRect(-1280, -200, 1280, 1024) };
	const wxRect saved(-1200, -100, 900, 700);
	ASSERT_TRUE((saved) == (ConstrainWindowRect(saved, areas, wxSize(320, 240))));
}
TEST(WindowGeometry, BoundsIncludeSizeAndTaskbar)
{
	const wxRect area(0, 40, 1280, 680);
	const std::vector<wxRect> areas{ area };
	ASSERT_TRUE((area) == (ConstrainWindowRect(wxRect(1200, 700, 8000, 4000), areas, wxSize(320, 240))));
	ASSERT_TRUE((wxSize(320, 240)) ==
		    (ConstrainWindowRect(wxRect(10, 50, 1, 1), areas, wxSize(320, 240)).GetSize()));
}
TEST(WindowGeometry, ExtremeSavedCoordinatesDoNotOverflow)
{
	const wxRect area(0, 0, 1280, 720);
	ASSERT_TRUE((area) == (ConstrainWindowRect(wxRect(std::numeric_limits<int>::max(),
							   std::numeric_limits<int>::min(),
							   std::numeric_limits<int>::max(),
							   std::numeric_limits<int>::max()),
				      { area },
				      wxSize(320, 240))));
}
TEST(WindowGeometry, MaximizedAndMinimizedStatesKeepNormalBounds)
{
	CWindowGeometry state;
	const wxRect normal(30, 40, 800, 600);
	state.Remember(normal, false, false);
	state.Remember(wxRect(0, 0, 1920, 1080), true, false);
	state.Remember(wxRect(-32000, -32000, 1, 1), false, true);
	ASSERT_TRUE((normal) == (state.GetNormalRect()));
	ASSERT_TRUE(state.IsMaximized());
	state.Remember(wxRect(50, 60, 900, 700), false, false);
	ASSERT_FALSE(state.IsMaximized());
	ASSERT_TRUE((wxRect(50, 60, 900, 700)) == (state.GetNormalRect()));
}
