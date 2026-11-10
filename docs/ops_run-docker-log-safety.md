# Bounded Docker log diagnostics and retention

Use `sudo -n /usr/local/sbin/docker-logs-safe CONTAINER [LINES]` on the Docker
host, including over SSH. It returns the last ten minutes, at most200 lines by
default, with an explicit maximum2000. Execution is limited to15s, followed by
SIGKILL after2s if TERM is ignored. Exit124 means timeout; forced kill may return137.
Keep this exit status visible when piping or aggregating diagnostics. Do not use
unbounded `docker logs -f` or rely on a local SSH timeout to stop remote children.

## Installation and rollout

From an accepted clean repository revision, deliver `deploy/docker-logs-safe` and
`deploy/configure_docker_logs.py` to a private release directory on production.
Install the wrapper root-owned, mode0755, at `/usr/local/sbin/docker-logs-safe`.
No application image rebuild or Docker daemon restart is needed.

Run `configure_docker_logs.py --compose PATH [PATH ...]` on the host first, then
repeat with `--apply`. The installer adds only a YAML anchor and logging entries,
preserving source text/interpolation. It compares resolved before/after Compose
configurations and refuses changes outside logging. Existing custom logging
policies require review. Backups and candidates are created privately because
Compose files may contain environment settings. Re-running is idempotent.

Policy: JSON-file, max-size20m, max-file3 (approximately60MiB per container).
This is finite history, not an archival guarantee. See the
[Docker logging reference](https://docs.docker.com/engine/logging/drivers/json-file/).

Existing containers require recreation to acquire log options. For each target:

1. Read its Compose project, working-directory and complete config-file chain
   from container labels; preserve every operator override. Save previous image,
   container ID and logging configuration. Retain a bounded private log snapshot
   before replacing the container; old Docker logs disappear on removal.
2. Resolve the complete configuration. Check current image ID, effective env,
   command, entrypoint, volumes, ports and identity against the running container.
   In particular preserve RabbitMQ hostname/node identity and its data volume.
3. Recreate only that service with `up -d --no-deps --no-build --pull never`.
   Check image, configuration, new logging options and application health before
   proceeding. Broker and gateway recreation briefly interrupts connections.
4. After broker restart check users, queues, consumers and reconnecting clients;
   after gateway replacement check nginx config and public endpoints. Verify
   application health again after the final dependency replacement.

Do not recreate optional E2E fixtures by inventing missing environment settings.
Leave excluded containers explicit in the release evidence. Never manipulate
Docker's JSON files to implement rotation.

Rollback: restore the private `*.pre-logging-*` Compose backup, preserve the same
complete override chain and image, recreate only the affected service, and verify
health. The diagnostic wrapper is independent of the Compose policy.

## Verification scope

Installer unit tests cover source preservation, idempotency and conflict rejection.
Linux integration must verify actual timeout/forced kill and absence of surviving
readers. Application tests/builds are unnecessary for logging-only configuration.
Verify effective LogConfig on the running containers; a valid YAML file alone does
not establish that rotation is active.
