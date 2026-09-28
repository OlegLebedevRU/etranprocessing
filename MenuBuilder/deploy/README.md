# Manual MenuBuilder release

Build on the approved build host from a published Git archive containing
`MenuBuilder/backend`, `MenuBuilder/frontend`, `MenuBuilder/nginx.conf`,
`MenuBuilder/deploy` and the unchanged accepted
`shared` build dependency. Do not copy runtime environment files into that archive.

1. Record the source commit and archive SHA-256; verify the accepted shared package
   manifest before building. Set `org.opencontainers.image.revision` on both images.
   Create the archive with `git -c core.autocrlf=false archive` and verify its
   files against Git blobs, so Windows checkout conversion cannot alter it.
2. Build backend Dockerfile target `verification`; require pytest, Ruff, format
   and Pyright success. Build target `release` from the same archive. The release
   image contains production dependencies only.
3. Build the frontend artifact image from `MenuBuilder/frontend/Dockerfile`;
   its build runs the frontend tests and TypeScript/Vite build.
   `build-release.sh` performs these builds and registry publication with
   `SOURCE_REVISION` and `REGISTRY_PREFIX` supplied by the release operator.
4. Scan image files and source for private configuration or credentials.
   Push both images to the approved private registry and record manifest digests.
5. On production, preserve the current backend image ID, private configuration,
   effective flags, frontend files and image selection for rollback. Pull both
   new images using their registry references with `@sha256:` digests.
6. Set `MENUBUILDER_BACKEND_IMAGE` in a private deployment environment file.
   Add `release.compose.yaml` after the existing production Compose file;
   run `up -d --no-build --no-deps menubuilder-backend`. For the isolated test
   Compose project use `test-release.compose.yaml` and service `test-backend`.
7. Create a temporary container from the frontend artifact image with a dummy
   command, copy `/dist` into a staging directory, then remove that container.
   Install the hashed assets first and replace `index.html` atomically last.
   Keep the previous hashed assets through the rollback window. The existing
   nginx bind mount needs no restart for content changes.
8. Verify effective digest, API health, tenant permissions, PIN operations,
   financial reconciliation and browser video start/stop. Commercial activation
   is a separate bounded phase; never infer live payment readiness from mocks.

Use `sudo docker` on the servers. There is no beta timer, beta worker,
`.etran-ci` image override, production build, or direct image save/load path.
Janus, ingress, IoT, ProcessingBackend and the Windows Agent are not rebuilt here.

For rollback, reselect the preserved backend image, restore only the affected
private MenuBuilder configuration and previous frontend index/assets, then repeat
the health and browser checks. Keep secrets out of commands printed to logs,
source control and evidence; back up private files with restrictive permissions.

For the explicitly authorized PIN integration, `configure-pin-auth.py` transfers
the existing provider service credential in memory into the MenuBuilder private
env file. Supply the verified provider container, env path and an empty rollback
directory with mode 700. It refuses to overwrite its backup, writes the new env
atomically with mode 600, and never prints credential values. Run it without shell
tracing; do not pipe its private subprocess output into logs. It does not change
the provider. Recreate the two MenuBuilder consumers using the approved images,
then check missing/wrong-auth 401 and authorized absent-operation 404. The rollback
is the saved private env plus the exact previous MenuBuilder image selections.

`verify-finance-rollback.py` is an explicitly invoked release check for existing
test tenant 10000, not a background worker. Run the published script from a
read-only mount in a disposable container using the exact released backend
image and the private runtime environment. Set a whole-container timeout of
120 seconds and remove the container afterwards. Do not copy it into a running
service. It checks a new monthly charge and its replay, a 600-second simulated
grace boundary, a mock stop and reconciliation inside an outer PostgreSQL
transaction. Service commits release savepoints; the outer transaction always
rolls back. Post-checks compare tenant balance and counts of ledger, sessions,
cycles, charges, reconciliation and audit rows. Sequence values may advance.
The test requires no live tenant session and refuses a concurrent session.
It proves service behavior with PostgreSQL, not a real provider stop or a
durable charge. Production commercial flags and provider settings stay unchanged.
