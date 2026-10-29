# Regular expressions in message filters

Preferences > Filters offers **Use a regular expression instead of the word list**
under the message content rule. The master switch and content rule must both be
on. Current chats remain exempt; **Filter all messages** still takes precedence.
The word list and regex are stored independently, so switching modes retains both.
File comments continue to use the existing comma-separated words.

## Examples

Regex mode searches anywhere in the message, ignoring case:

| Expression | Matches | Does not match |
| --- | --- | --- |
| `dog.*` | `dog`, `doghouse`, `hotDOG` | `cat` |
| `^dog.*` | `doghouse` | `hotdog` |
| `spam$` | `this is SPAM` | `spam alert` |
| `dog\|cat` | `hotdog`, `cat` | `bird` |
| `[0-9]{2,4}` | `number 123` | `number 7` |
| `dog\.` | `dog.` | `dogs` |

Commas are not list separators in regex mode. Spaces are significant, including
leading and trailing spaces. By default, `.` does not match a newline, and `^`/`$`
anchor the whole message. RE2 inline flags may explicitly change these defaults.
The syntax is documented at <https://github.com/google/re2/wiki/Syntax>.
Lookaround and backreferences are unsupported.

A bare `*` is an error in regex mode. The legacy word list of exactly `*` continues
to block all messages in Words mode. Expressions such as `.*` and `a*` match empty
text; the live summary explains that these can match every message.

The summary updates while editing. Invalid expressions appear there; applying
invalid regex settings keeps the dialog open and leaves the applied configuration
unchanged. The daemon validates remote changes before applying any preferences
from that packet. A malformed enabled regex in a hand-edited configuration is
disabled on startup and reported in the log, with its text retained for correction.

## Shared implementation

`src/libs/common/Regex.h` provides `CRegex`, an immutable compiled expression with
`Search`, `FullMatch`, `MatchesEmpty`, and diagnostic accessors. It knows nothing
about messages, preferences, or discard/hide policies. Future text filters can
use it without changing the existing message or file-comment rules.

The wrapper uses RE2 with UTF-8 input. Case sensitivity is an explicit option;
the general-purpose default is case-sensitive, while message filtering chooses
case-insensitive matching. The default maximum expression size is 4 KiB of UTF-8
and the RE2 memory budget is 8 MiB. Callers can supply different limits. Compiled
expressions can be searched concurrently. Message filtering caches compilation
when preferences change rather than compiling for every incoming message.

RE2 is a build dependency. CMake prefers its exported target, including transitive
Abseil dependencies on newer releases, and supports older packages via pkg-config.
See [INSTALL.md](INSTALL.md) for package names.

## Remote compatibility

EC exposes explicit-value `EC_TAG_MSGFILTER_REGEX_ENABLED` and
`EC_TAG_MSGFILTER_REGEX_PATTERN` tags. Their presence advertises support. Omission
on a preference update preserves the corresponding stored setting. New desktop
and Web UI clients disable regex controls when the daemon lacks these tags.

The REST preference fields are `message_filter.regex_supported` (read-only),
`message_filter.use_regex`, and `message_filter.regex_pattern`. Invalid expressions
are rejected by the daemon; clients must surface its error rather than reporting
a successful save. Older clients may still edit the retained word list, but must
upgrade to display or change the active regex settings.
