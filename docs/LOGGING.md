# Logging format

Core logs and the console logger use UTF-8 text with an explicit severity:

```text
2026-10-18T17:42:31.123+08:00 INFO  Disconnected from eD2k
2026-10-18T17:42:31.124+08:00 WARN  [wx] A warning
```

The timestamp uses the emitting process's local time with milliseconds and a
numeric UTC offset (`+08:00` in this example). This keeps desktop times familiar
and makes server logs unambiguous, including across daylight-saving transitions.
Millisecond precision distinguishes bursts of messages within the same second.
The instant is captured before the core logger queues a worker-thread message.
Remote clients display the server's time and offset. Each physical message line,
including interior blank lines and entirely blank records, receives a prefix.
Severity names occupy five columns; ordinary core messages have no subsystem tag. Named debug categories and explicit subsystems
retain their tags.

The levels are TRACE, DEBUG, INFO, WARN and ERROR. This change does not add a
runtime level threshold or enable debug traces in release builds.

Severity, category and status-bar visibility are independent. Existing
`AddLogLineC` and `AddDebugLogLineC` calls remain INFO until their severity is
explicitly classified: their historical C flag requests a status-bar update,
not a warning. Noncritical debug messages use DEBUG and retain their existing
category checks. New code can use `CLogger::Log` with an explicit category and
severity, or `AddLogLineWarning` / `AddLogLineError`. These macros do not request
a status-bar update. wxWidgets records retain their native severity and warnings
and errors continue to update the status bar.

The core logfile starts with the timestamp. After the severity, it stores the
independent status-bar and EC replay flags in a reserved header field:

```text
2026-10-18T17:42:31.123+08:00 INFO  [log status=1 replay=1] Disconnected from eD2k
2026-10-18T17:42:31.124+08:00 DEBUG [log status=0 replay=0] [FileIO] File detail
```

`status=1` requests a status-bar update; `replay=0` excludes the line from
incremental GUI replay. Full-log/debug snapshots retain logfile-only records with
their historical `.` wire marker.
Every core file record includes this field, before the category/message, so text
inside a message cannot impersonate the flags. The flags stay with the record
across append, backup, and reset without a sidecar file. Stdout and GUI text omit
the field. For GUI/EC delivery only, the logger reconstructs the historical first
byte: space for ordinary messages and `!` for status-bar messages. Older remote
GUIs can keep stripping that byte without an EC protocol revision. Replay also
accepts pre-upgrade marker files (including `.` logfile-only lines), since a failed
backup can leave old and new records in the same file. The `amulecmd` log command,
Web UI log output and API log strings strip the EC marker before exporting text,
so their records start with the timestamp too.

EmergencyLog records use ERROR and the `crash` category, with timestamp-first
file/stderr lines. Low-level fatal signal reports still write directly to their
reserved descriptors: signal handlers cannot safely use wxWidgets formatting.
Their raw backtrace text remains recoverable over EC without losing a first byte.

The standalone console logger shares the formatter. Its category identifies the
program (`amulecmd`, `amuleweb` or `amuleapi`). The connector initializes it once
from its declared program name before starting workers, so renaming the executable
or invoking it through a symlink does not change the tag;
explicit `CLogger::Log` calls can supply a subsystem. Debug builds retain source
file/line details in legacy messages.

## Follow-up work

The amuleapi descriptor tee still timestamps arbitrary stdout/stderr, including
startup messages and crash output. Direct diagnostics use the shared formatter
with explicit WARN/ERROR severity,
and the wx log target preserves native severity and emission time. Raw descriptor
output receives the shared INFO prefix and the `amuleapi` tag. The tee recognizes
the shared local-time prefix with its numeric offset across pipe-read boundaries
to avoid double timestamps on console logger records, while preserving
raw-output capture and the crash descriptor. Blank raw lines are timestamped too.
Rotation and the EC protocol remain unchanged.

Runtime severity/category controls, classifying legacy status messages, optional
JSON Lines output, and core logfile rotation can be introduced separately.

## Review sequence for 3.2.0

The format and sequence were agreed in #1441, including RFC 3339 offsets, shared
API formatting, and timestamp-first core files. The format changes GUI, status-bar, Web UI, EC/API and file output, and belongs to 3.2.0.
The sequence is to review severity/format independently, then add
runtime debug categories in Release with a benchmark against the current Release
build with every category disabled. That follow-up must keep message construction
behind the category check, preserve existing debug-only diagnostic blocks, and
measure the disabled path before enabling it in production. It should include
thread-safe category controls and their preferences/EC integration. This change
makes no claim about the performance of runtime categories; those follow separately
in #2021.
