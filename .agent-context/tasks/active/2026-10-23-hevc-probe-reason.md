# HEVC inventory reason correction

Owner reported profile_unsupported on terminal773 after an actual1.13.46 HEVC
decode check. Current agent evidence was temporarily_unavailable/probe_timeout,
not proof that the selected Medium profile or hardware was unsupported.

MenuBuilder's profile selector now preserves a validated non-ready HEVC reason.
A ready capability without the requested profile still returns profile_unsupported.
No new schema, permission, fallback, lease or database behavior. IoT owns the
paired10s capability negotiation window; native l4tools owns priority probing.

Local checks:44 focused HEVC/video-control/watch backend tests passed, Ruff and
format passed, Pyright app0errors. Frontend and ProcessingBackend unchanged;
their checks were not run. Builder verification passed829 tests,22 skipped.
Runtime delivery is verified: source780756863e162ec6e2d7ba85afbbec32e80c0d6a,
image sha256:c114aa8e33fe8bc1ebb51e182b51949be3faf8622ad6cdef2c3fe58a6adcfdf3
for MenuBuilder backend and its paired JWT issuer signer. No frontend or
ProcessingBackend rollout. Post-idle installed-agent acceptance is separate;
do not claim the current host fixed from backend checks alone.

Initial rollout omitted the existing operator Compose override, producing
disabled_by_policy despite healthy containers. Restoring that same file with
the same published digest restored policy for the single already-approved SN.
Final snapshot: all13 environment hashes equal the pre-rollout snapshot; ten
unrelated containers retain their IDs/images. The standard launcher does not
automatically retain arbitrary operator overrides; the release runbook now
records the required before/after check. This is an operational limitation,
not a new global HEVC policy or compatibility admission.

Installed native1.13.48 source39e60cafa6ba9301309bc1f04cf8424a188e5e56 after
the owner's explicit selection. Normal authenticated MenuBuilder checks decoded
H265 for60s (296 frames), then45s (204 frames) after71s idle; H26430s (85 frames).
No observed frame-growth stops. This verifies the reported773 inventory failure
path; it does not close the broader TASK-60 startup/role/OS acceptance findings.

Git synchronization also preserved/published the owner's89 local docs/site files
on etranprocessing main; site syntax and six-page build passed. Original dirty
state retained in the named sync stash, whose SHA is in the external plan folder.
