# Media source builds and registry deployment

Build both media images on the approved build host. Production pulls tested
registry digests; it does not compile Janus or ingress. Registry endpoints and
credentials belong to the operator environment, not this repository.

## Source and build

1. Commit and push the reviewed media source. Export only `l4media/` with
   `git archive <full-commit> l4media`. Calculate SHA-256 of the archive, transfer
   it to a dedicated build directory, verify that digest, and extract it.
2. Set `SOURCE_REVISION` (full commit), `SOURCE_ARCHIVE_SHA256` (verified archive),
   and `REGISTRY_PREFIX` (registry plus optional namespace). Run
   `bash l4media/deploy/build-publish.sh` from the extracted release.
3. The script builds both images inside Docker, runs isolated synthetic tests,
   then pushes `l4media-ingress:git-<commit>` and `l4media-janus:git-<commit>`.
   Save build/test logs and registry digests with the release evidence.
   Reusing an existing release tag for different bytes is prohibited.

Janus is compiled from upstream commit
`3c39ce8cf11c54cf6f1607030a47ac9db798389a` (1.1.4), matching the previous
production version. The source archive checksum and base OS digest are pinned
in its Dockerfile. Runtime includes the upstream source archive and GPL license.
Streaming, HTTP, WebSocket and data-channel support are compiled; unused
plugins/transports/handlers are disabled. An upstream version upgrade is a
separate reviewed change. OS packages are resolved at build time: this is not
a claim of bit-for-bit reproducibility across rebuild dates. Deploy the exact
tested digest, never a fresh rebuild of the same tag.

Ingress compiles from this repository and runs C unit tests during its build.
Both images carry source commit/archive labels. `smoke-images.sh` also verifies
that missing private credentials cause exit 1, and tests lifecycle and RTP
contracts in an internal Docker network without production credentials or
published ports. Test containers, network and synthetic secrets are cleaned up.
Browser decoded video remains a separate production acceptance check.

## Private configuration and coordinated activation

The private media `.env` must provide distinct randomly generated credentials
`L4MEDIA_SERVICE_TOKEN` and `JANUS_ADMIN_SECRET` (32–127 ASCII alphanumeric,
underscore or hyphen characters; 64 hexadecimal characters are recommended).
No public default is accepted. Keep the file and private backups mode 600.
The Janus entrypoint renders its token placeholder into a mode-600 runtime file
under `/run/l4media-janus`; no secret belongs in the image or tracked config.

Set `L4MEDIA_INGRESS_IMAGE` and `L4MEDIA_JANUS_IMAGE` to the published
`registry/repository@sha256:<digest>` references in the private release config.
Before activation, record the running image IDs, affected container IDs,
configuration versions and private backup locations. Confirm no active streams
will be interrupted without agreement. Preserve existing archive data and TLS
files; keep archive-worker/purge disabled unless separately authorized.

Credential rotation requires the separately approved MenuBuilder consumer step.
Prepare both provider images/configs and all confirmed consumer private env paths
before switching. Use one private shared token for ingress and its consumers,
and a different private Janus secret. Never transfer production credentials to
the build host or include their values/hashes in logs or handoffs.

After validating Compose without printing its expanded environment, pull the
two digest references and activate only `janus ingress` with
`sudo docker compose up -d --no-build --no-deps janus ingress`.
The authorized consumer owner then updates/recreates only affected backends.
Verify missing/wrong token denial, an authorized read-only operation, backend
health, unrelated container identity, and a fresh browser start/stop.

Rollback restores both old images AND corresponding private provider/consumer
configuration together. An old Janus image needs its original config, not the
new placeholder template. Preserve rollback images locally before replacement;
do not remove them as build cleanup. Document rollback readiness separately
from an actually executed rollback.

`deploy.sh` is the earlier bootstrap workflow and builds on the destination;
it is not the registry release procedure above.
