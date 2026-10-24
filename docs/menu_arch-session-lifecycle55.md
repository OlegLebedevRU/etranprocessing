# Session lifecycle: bounded renewal and honest termination

Owner instruction: deploy changed stacks, then accept together on773. Native
unsigned local pilot is separately authorized; unchanged binaries stay intact.
MB owns browser scheduling and user workflow; IoT owns lease; native owns producer.
No DB migration, camera writer enablement or input authorization change.

The browser renew operation has an8second total budget including auth queue wait,
an AbortSignal preserved on retries, at most3 sequential failed attempts and the
existing5second normal/2second retry intervals. Stop/replacement aborts in-flight
renew. Late callbacks cannot change a newer generation. Timer lateness and total
request duration are console diagnostics without credentials. These changes close
unbounded waiting, but do not prove the cause of the observed75second request gap.

START timestamps are assigned after media preparation. Producer readiness and
immutable descriptor checks remain required. Desktop v2 STOP HTTP success means
server session closure, not confirmed physical producer exit; MB no longer books
the unconfirmed final accounting interval as a confirmed stop. Wire schemas and
ownership checks are unchanged. IoT handoff uses the same distinction.

Focused checks: frontend coordinator tests, frontend build, backend HEVC/control
tests, Ruff and Pyright. Live acceptance follows deployment of all changed stacks.
Do not advertise this document as live acceptance before the browser run.
