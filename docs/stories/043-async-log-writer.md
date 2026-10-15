# 043 - Log files written on their own thread

Counterpart of [ReHLDS MT-301](https://github.com/aluzed/ReHLDS-MT/blob/master/docs/stories/MT-301-async-log-writer.md).

## Context
`Log_Printf` (`engine/server/sv_log.c`) writes and flushes the server log
on the main thread; `-condebug` writes the console log the same way. A slow
disk stalls the frame.

## Goal
`sv_log_async 1`: lines are queued and written by a writer thread;
`sv_log_async_overflow block|drop`, `sv_log_async_stats`. Same line order and
content.

## Progress (2026-10-15)
- [x] `engine/common/async_log.c`, port of the ReHLDS writer: bounded 4 MB
      queue, one writer thread (`xash-log`), `sv_log_async` (off by default,
      dedicated server only, applies to the next log file),
      `sv_log_async_overflow block|drop`, `sv_log_async_stats`.
- [x] Server log file (`Log_Printf`): the main thread still creates the file
      (`Log_Open`), the writer appends to it; `Log_Close` writes out the queue
      before closing.
- [x] Console log (`-log`, the counterpart of `-condebug`): `Sys_PrintLog`
      queues the line for the log's file descriptor instead of `write` +
      `fflush`; stdout stays on the calling thread. Switching
      `sv_log_async` off writes the queue out first. `Host_Shutdown` writes
      everything out before the last lines.
- [x] Checked on the 034 server: 744 lines in 70 s, nothing dropped, the main
      thread never waited, both files complete and closed.
- Threads use `engine/common/sys_thread.h` (pthreads or Win32, the dedicated
  build has no SDL), shared with the network threads to come.
- Not measured against a slow disk: like ReHLDS, `log_write` is 0.0 µs on
  average with the files on a local disk; the option protects against
  stalls, it doesn't speed up a normal frame.

## Dependencies
035.
