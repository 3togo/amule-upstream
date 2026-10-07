# Background loading of Kad indexes

Starting Kad launches a worker to load `key_index.dat`, `src_index.dat`, and `load_index.dat`. The main thread continues routing and processing searches. It adopts a complete index during the next Kad processing tick; the worker never mutates the live index, application preferences, or Kad session. Configuration directory and Kad identity are captured before dispatch.

Publisher trust counts belong to each index. Loaded keyword entries and newly received publications use that index's tracking table, preventing loading work from modifying live publisher counts. The existing index formats and AICH preference behavior are preserved.

While loading, the core does not store or serve indexed entries and does not acknowledge publish requests as successful. Index counters remain zero until adoption; EC reports the adopted counts even before Kad has bootstrapped. Loading failures discard partial state, report an error, and leave persisted files intact. A failed index remains unavailable for the session; repair or remove the offending saved index and restart Kad to retry. An absent index is a normal empty index; an unreadable, truncated, or unsupported-version file is a failure.

Cancellation is checked inside keyword hash, filename, and publisher lists as well as between entries. Stopping Kad requests cooperative cancellation and joins the worker before releasing state. A stop before adoption does not rewrite the saved indexes. After successful adoption, normal shutdown writes each index to a staging file, flushes it, and replaces the corresponding saved file only after a successful close. A failed open, disk-full write, flush, or replacement preserves that file’s previous contents and reports the save failure. The three files are committed independently. Memory cleanup runs even if a file write fails.

The behavior was informed by eMule's asynchronous index loading in reference checkout `ac3d52e`. aMule's implementation uses an owned background result, a main-thread handoff, and per-index publisher tracking; no eMule thread or loader implementation was transplanted.

Additional regression coverage exercises 10,000 keywords and 10,000 sources through adoption and a save/reload cycle, rejects publications before adoption, and preserves an existing load index when staging-file creation fails or a Linux `/dev/full` write fails.
