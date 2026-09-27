# L4D-18B-PB-FIX-01 — ProcessingBackend production rollout

Producer verdict: **ACCEPTED** for the ProcessingBackend rollout and its
checks. The shared handoff remains **PENDING** independent controller review;
this report does not accept its own handoff. Final candidate:
`ProcessingBackend/docs/l4desk/handoffs/L4D-18B-PB-FIX-01-candidate.md`.

## Scope and immutable inputs

- Scope: `ProcessingBackend` only. Branch: `release/l4tools-1.8.2-beta-1`.
- Corrective registration `R-L4D-18B-PB-FIX-01-v1`, published in
  `551f5c7998bc20f5361cbdf10709e6d0b1c7f995`. The required direct inputs
  are unique, ACCEPTED, version `1.0.0`, addressed by that registration,
  compatible and not revoked.
- `H-L4D-18A-SHARED-v1`: producer
  `537a1e493c83d1fa8e8cb765228be8d1b24a1d62`, accepted Git-source
  `etranprocessing-db==0.1.1`; source manifest SHA-256
  `364efa7b369cdcb8da12025b377518834b6013fd693a28f321a81dcbe18a68c6`.
  This is also the 18A sequence gate.
- `H-L4D-18B-PB-EVIDENCE-CONTRACT-01-v1`: producer
  `61d79a9ae9790ab6eee991ad64539e893aab6ade`, data-only export of the
  historical PB reports and PIN contract `1.0.0`. It is not runtime evidence.
- All **12/12** explicitly granted external artifacts were compared against
  their accepted SHA-256 using Git/raw bytes at the registered commits.
  Historical 17B/04B/06A blocks were provenance only; their old digest defects
  were neither rewritten nor silently normalized.

## Release files and image provenance

| Item | Exact result |
|---|---|
| PB runtime source | `551f5c7998bc20f5361cbdf10709e6d0b1c7f995`; app, Alembic, Dockerfile and lock unchanged since accepted PB source `083138f223b723098e9a188803e3fc802e8a6011` |
| Shared source | The accepted 0.1.1 source/config tree is unchanged from `537a1e493c83d1fa8e8cb765228be8d1b24a1d62` |
| Git/raw build archive | SHA-256 `1ef3130a49573016cdbf4c63d81b5b56a0960d39d2e824203bd176aef61e12de`; `git -c core.autocrlf=false archive` from PB and accepted shared trees |
| Running image | `sha256:8b61162f2b7544a9f733bf4bdddf1bf063c3e3e99034438c5e82f82e042ae43a`; immutable tag `user1-processing-backend:18b-551f5c7-gitraw` and revision/archive SHA labels match the row above |
| Installed packages | `processing-backend==0.1.0`, `etranprocessing-db==0.1.1`; Python 3.14.7, pinned uv lock |
| Schema | Alembic `027 (head)` before and after; no migration was required and no DDL or downgrade was run |
| Public ingress change | `f5dc263`: exact and prefix PIN service paths now return nginx 404; live config SHA-256 `d896848147230f514bd76f2e26b0269bbe9ae3c868367fe083942f2ce7e43d13`, equal to its Git/raw blob |
| Configuration template | `67827b6`: empty `SERVICE_AUTH_TOKEN=` placeholder only; no secret committed |

The first isolated build used Windows Git's CRLF archive conversion. It passed
functional checks, but its source bytes differed from Git blobs. It was
replaced by the Git/raw image above and its temporary image was removed. Four
representative running app/migration files were checked against exact Git
blob SHA-256, including the certificate router and revision 027. The running
image uses the newer uv/`/workspace` Docker layout; the previous image used
pip/`/app`. This difference was tested, not assumed equivalent.
The nginx configuration was likewise reinstalled from Git/raw bytes after its
first Windows archive had CRLF conversion; its live SHA now matches the blob.

## Validation and production readiness

- Local `uv run --locked ruff check --fix app alembic tests`: pass.
- Local `uv run --locked ruff format app alembic tests`: 77 files unchanged.
- Local `uv run --locked pyright app alembic`: 0 errors, 0 warnings.
- Local full `uv run --locked pytest -q --basetemp=.pytest_tmp_18b_final`:
  **130 passed**, 20 pre-existing mock/TestClient warnings; no failed test.
- Local `uv build --offline`: wheel and sdist built successfully.
- Changed tracked files passed `git diff --check` and a read-only credential
  pattern scan; no secret value entered Git.
- MCP Ops tools were unavailable in this session:
  **[MCP Ops Readiness: UNAVAILABLE]**. The approved noninteractive SSH path
  was used. Before rollout available RAM was about 2 GiB, load 0.11, and root
  disk 88%; after cleanup it is 89%.
- Managed PostgreSQL 18 accepted connections. A pre-rollout `pg_dump -Fc`
  was made with PostgreSQL 18 client tooling; `pg_restore -l` read 569 TOC
  entries. Backup:
  `/home/user1/.l4d-releases/18b-551f5c7/etran-pre-18b.dump`,
  SHA-256 `4ff0d4313a435bd99377417b4fcadbd9399a66affc414b9ed4dedf38f027b846`,
  mode 600. The host's PostgreSQL 16 client was incompatible and was not
  used for this dump.
- Previous compatible image `sha256:e2194a9b3341ec24d5b0d176d825f6879eff6d7c074bb107276473c28936e511`
  remains tagged `user1-processing-backend:17f-accepted-baseline`. The
  previous nginx file is saved in the release staging directory.

## Production smoke and security boundary

- Only `processing-backend` was recreated. It is running the exact image
  above with restart count 0; recent logs contain no ERROR/Traceback.
- Health: 200, `{"status":"ok"}`. Alembic current and heads: 027.
- Existing terminal XML `function=check` without PIN: HTTP 200, XML
  `code=2`, same as the pre-rollout response. No certificate was issued.
- Service PIN API is available only inside the service network. A 256-bit
  random `SERVICE_AUTH_TOKEN` was added with the owner's explicit approval
  to the private server `.env` (mode 600), then only PB was recreated.
  No token value was printed, copied into evidence or committed. Internal
  GET/POST without token: 401 `SERVICE_AUTH_FAILED`. Authorized GET of an
  unknown operation: 404 `OPERATION_NOT_FOUND`. Authorized POST for a
  nonexistent terminal: 404 `TERMINAL_NOT_FOUND`; no PIN was created.
- All public nginx ingress routes to PB were inspected. Public GET and POST
  under `/api/certificates/pins/`, plus the exact `/api/certificates/pins`
  path, return nginx 404 `text/html`; an external direct probe confirmed
  this. `nginx -t` passed before reload and again afterward. The legacy
  `/certificates/` XML path still returns its original response.
- The temporary copy of the private `.env` was deleted after successful
  smoke. Its secret remains only in the live private server configuration.
  The future IoT consumer must obtain it via approved private operations;
  it is intentionally absent from this handoff.

## Rollback and remaining limits

Rollback is **ready, not executed**. Re-tag the preserved baseline image as
`user1-processing-backend:latest`, recreate only `processing-backend` with
`sudo docker compose -f /home/user1/compose.yaml up -d --no-deps --no-build
--force-recreate processing-backend`, and verify health and revision 027.
Keep the nginx public PIN denial and private service token: the baseline
application supports the same token dependency. Restore the saved nginx file
only if its separate XML compatibility check fails; never downgrade populated
schema 027.

No live PIN issuance or positive terminal enrollment was performed; the
production probes covered safe error paths. Existing local provider contract
tests and prior accepted terminal smoke cover those behaviors. The 20 pytest
warnings concern existing asynchronous mocks and TestClient deprecation.
The next consumer must configure the private service credential before
requesting a PIN.

The tracked checkout has no unrelated diff. Two untracked local pytest temp
directories from this task remain: automatic approval review rejected both
safe cleanup attempts with `blocked by policy`. They are outside published
R/C, image and artifact digests; manual local housekeeping remains.

Output candidate is `H-L4D-18B-PB-v1`, addressed to
`L4D-18C-IOT` and `L4D-18E-MB`. Only the independent controller may append
ACCEPTED to the common journal.
