# MegaWiFi API Bug Fix: mw_http_cleanup() wrong command

## File modified
`~/sgdk/src/ext/mw/megawifi.c`

## The bug
`mw_http_cleanup()` was sending `MW_CMD_HTTP_FINISH` (cmd 46) to the ESP32
instead of the correct `MW_CMD_HTTP_CLEANUP` (cmd 47).

The protocol defines two distinct commands:
- `MW_CMD_HTTP_FINISH  = 46` — "done sending body, give me the response headers"
- `MW_CMD_HTTP_CLEANUP = 47` — "reset HTTP state machine to idle"

`mw_http_cleanup()` is supposed to reset the ESP32 HTTP state machine so the
next request can start clean. By sending FINISH (46) instead of CLEANUP (47),
the ESP32 would receive a FINISH with no open HTTP session, reject it, and leave
the HTTP state machine in a broken state. Every subsequent HTTP command
(cert_query, url_set, etc.) would then return MW_ERR (1).

## The fix
```c
// megawifi.c — mw_http_cleanup()
// Before:
d.cmd->cmd = MW_CMD_HTTP_FINISH;

// After:
d.cmd->cmd = MW_CMD_HTTP_CLEANUP;
```

## Impact
Without this fix, all HTTP operations fail from the very first attempt.
The mw_http_cleanup() call that precedes every request was poisoning the
firmware state machine rather than resetting it.

## Discovery
Spotted by comparing the command enum in mw-msg.h (which lists both 46 and 47
as separate commands) against the megawifi.c implementation, which never
referenced MW_CMD_HTTP_CLEANUP at all.

## Note
This fix is applied to the local SGDK copy at `~/sgdk`. It must be reapplied
if SGDK is ever replaced or updated.
