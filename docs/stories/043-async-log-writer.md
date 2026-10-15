# 043 - Log files written on their own thread

Counterpart of [ReHLDS MT-301](https://github.com/aluzed/ReHLDS/blob/feature/multithreading/docs/stories/MT-301-async-log-writer.md).

## Context
`Log_Printf` (`engine/server/sv_log.c`) writes and flushes the server log
on the main thread; `-condebug` writes the console log the same way. A slow
disk stalls the frame.

## Goal
`sv_log_async 1`: lines are queued and written by a writer thread;
`sv_log_async_overflow block|drop`, `sv_log_async_stats`. Same line order and
content.

## Dependencies
035.
