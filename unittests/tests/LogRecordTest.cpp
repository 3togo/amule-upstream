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
#include "Logger.h"
#include <memory>

using namespace muleunit;
DECLARE_SIMPLE(LogRecord)

namespace
{
wxDateTime FixedTime()
{
	wxDateTime time;
	time.Set((time_t)0);
	time.SetMillisecond(123);
	return time;
}
std::string Text(const wxString &text)
{
	return std::string(text.utf8_str());
}
} // namespace

TEST(LogRecord, FormatsUtcMillisecondsAndUtf8)
{
	const CLogRecord record(LogLevel::Warning, "FileIO", wxString::FromUTF8("café"), FixedTime());
	ASSERT_EQUALS(
		std::string("1970-01-01T00:00:00.123Z WARN [FileIO] café\n"), Text(FormatLogLines(record)));
}

TEST(LogRecord, EverySeverityHasAnExplicitName)
{
	const LogLevel levels[] = {
		LogLevel::Trace, LogLevel::Debug, LogLevel::Info, LogLevel::Warning, LogLevel::Error
	};
	const char *names[] = { "TRACE", "DEBUG", "INFO", "WARN", "ERROR" };
	for (unsigned i = 0; i < 5; ++i) {
		const CLogRecord record(levels[i], "core", "message", FixedTime());
		ASSERT_EQUALS(std::string("1970-01-01T00:00:00.123Z ") + names[i] + " [core] message\n",
			Text(FormatLogLines(record)));
	}
}

TEST(LogRecord, PrefixesEveryPhysicalLine)
{
	const CLogRecord record(LogLevel::Info, "core", "first\nsecond\n", FixedTime());
	ASSERT_EQUALS(std::string("1970-01-01T00:00:00.123Z INFO [core] first\n"
				  "1970-01-01T00:00:00.123Z INFO [core] second\n"),
		Text(FormatLogLines(record)));
	ASSERT_EQUALS(std::string("\n"),
		Text(FormatLogLines(CLogRecord(LogLevel::Info, "core", " \n\t", FixedTime()))));
}

TEST(LogRecord, MarkersEncodeVisibilityRatherThanSeverity)
{
	ASSERT_EQUALS(' ', LegacyLogMarker(false, true));
	ASSERT_EQUALS('!', LegacyLogMarker(true, true));
	ASSERT_EQUALS('.', LegacyLogMarker(false, false));
	ASSERT_EQUALS('.', LegacyLogMarker(true, false));
}

TEST(LogRecord, EventCloneRetainsEmissionTimeAndIndependentStorage)
{
	CLogRecord record(LogLevel::Error, "FileIO", "original", FixedTime());
	CLoggingEvent event(false, true, true, record);
	std::unique_ptr<wxEvent> clone(event.Clone());
	const auto &copy = static_cast<const CLoggingEvent &>(*clone);
	record.message = "changed after queueing";
	record.category = "changed";
	record.timestamp = wxDateTime::UNow();
	ASSERT_EQUALS(std::string("1970-01-01T00:00:00.123Z ERROR [FileIO] original\n"),
		Text(FormatLogLines(copy.Record())));
	ASSERT_TRUE(!copy.IsCritical());
	ASSERT_TRUE(copy.ToStdout());
	ASSERT_TRUE(copy.ToGUI());
	ASSERT_TRUE(static_cast<const wxChar *>(copy.Record().message.c_str()) !=
		    static_cast<const wxChar *>(event.Record().message.c_str()));
}
