# Logging format

Core logs and the console logger use UTF-8 text with an explicit severity:

```text
2026-10-18T09:42:31.123Z INFO [core] Disconnected from eD2k
2026-10-18T09:42:31.124Z WARN [wx] A warning
```

The timestamp is UTC with milliseconds and is captured before the core logger
queues a worker-thread message. Each physical message line receives a prefix.
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

The core logfile and GUI/EC delivery retain their historical first byte:
space for ordinary messages, `!` for status-bar messages and `.` for logfile-only
messages. Logfile-only messages remain excluded from EC replay. Human stdout
omits this byte. Existing remote GUIs can continue stripping the marker.

The standalone console logger shares the formatter. Its category is `console`;
explicit `CLogger::Log` calls can supply a subsystem. Debug builds retain source
file/line details in legacy messages.

## Follow-up work

The amuleapi descriptor tee still timestamps arbitrary stdout/stderr, including
startup messages and crash output. Its direct diagnostics and wxLogStderr target
retain their current format; migrating them is separate work. The tee recognizes
the shared UTC prefix across pipe-read boundaries to avoid double timestamps on
console logger records, while preserving raw-output capture and the crash
descriptor. Rotation, the EC protocol and crash output remain unchanged.

Runtime severity/category controls, classifying legacy status messages, optional
JSON Lines output, and core logfile rotation can be introduced separately.
