# l4media camera reader

TASK-20 implements the accepted `siplite-camera/1.0.0` contract separately from
frozen `hevc-contract/2.0.0`. The same service API, lease/session table, Redis
ownership, TLS/L4RTP ingress, RTP forwarding and Janus image remain in use.
There is no transcoder and no new desktop profile policy.

## Admission and capabilities

`L4MEDIA_CAMERA_CONTRACT_ENABLED=1` enables new camera writers/capabilities;
its default is0. `L4MEDIA_HEVC_ENABLED=1` remains independently necessary for
HEVC. The camera schema is loaded independently of both flags so existing
extended sessions can be read/renewed/stopped/restored while new writers are off.

Service requests explicitly negotiate
`X-L4-Camera-Contract: siplite-camera/1.0.0`. Unknown or duplicate headers return
422. Header matching is case-insensitive for the name, exact for the revision,
and limited to the HTTP header section. A revision appearing inside JSON cannot
negotiate support. Existing service authentication applies before negotiation.

Without the header, capabilities and old requests retain the frozen projection.
Extended START without negotiation is422; extended health/renew/stop likewise
requires the header and correct epoch identity. Invalid/mismatched identity
does not gain access by negotiating. Feature-off does not strand valid cleanup.

Negotiated capabilities append `camera_contract_revisions` and `camera_formats`.
The camera probe creates, reads back and deletes disposable Janus mountpoints
using the exact H.264 baseline/main/high and HEVC level153 fmtp strings. It never
adopts/deletes an existing mountpoint. Probe outcomes are cached for30 seconds
and invalidated when dependencies fail. Additional format probes run only while
the media table is idle, keeping synchronous control roundtrips out of live RTP;
cached format facts belong to the fixed Janus deployment. A format's `runtime_verified` means this
running l4media/Janus format probe passed; it does not mean a browser decoded a
camera frame. The client still checks camera/relay/browser readiness, and
fresh RTP/framesDecoded remain distinct observations after START.

The C validator evaluates the separate camera schema with an explicit root,
without swapping the global frozen schema. It implements the new H.264 profile
patterns and dependentRequired checks; existing v2 requests keep their prior
definitions. Camera descriptors preserve their real raster/profile/level;
the formatter does not rewrite VPS/SPS or encoded image data.

## Persistence and OpenAPI

The full immutable descriptor, including camera_contract_revision, is preserved
in existing Redis request records. Restore chooses the schema from the stored
descriptor. Missing camera reader, unknown revision or malformed descriptor
does not turn the record into legacy H.264. Existing epoch/lease cleanup applies.

The negotiated `/api/v2/openapi.json` projection contains only definitions
reachable from its service paths (54825 bytes), preserving the accepted package
definitions without exceeding the existing64KiB HTTP response buffer. The
frozen OpenAPI projection is unchanged. Runtime schema, fixture and OpenAPI
copies are pinned by `spec/camera-v1/SHA256SUMS`; Docker's build runs those checks
and both old/new production-C tests before producing an ingress image.

## Validation and release

New C tests cover the13 media vectors, header boundaries/duplicates, feature
gates, actual level123/153 descriptors, H.264 High, no down-projection,
feature-off cleanup and stale epochs. An isolated real Redis additionally
verifies full descriptor restoration and fail-closed behavior without a reader.
Existing unit/feedback/deadline/frozen-v2 tests remain required.

Only ingress is rebuilt/deployed; the accepted Janus digest is reused. Build
source on the approved builder, publish immutable registry digest and use the
scoped [camera reader activator](../l4media/deploy/activate_camera_reader.py).
It validates the image's full source revision, preserves the running Compose
overlay order and public HEVC flags, activates only ingress and rolls back on
failed health/capability/image checks. It refuses activation while streams are
active and verifies that adjacent containers retain their identities. Its
private release records contain no copied service credentials. The general
image-only deployer omits previously installed flag overlays and must not be
used to silently drop the existing HEVC configuration in this release.
Do not invoke the legacy deploy script
to recreate the entire stack. Runtime flags, deployed image/source identities
and smoke evidence belong in the release handoff; local tests alone are not
production readiness. Camera decode and siplite4623 remain the final hardware
and browser gates.
