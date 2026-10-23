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
their checks were not run. Runtime delivery and post-idle installed-agent
acceptance remain pending; do not claim the current host fixed from unit tests.

Git synchronization also preserved/published the owner's89 local docs/site files
on etranprocessing main; site syntax and six-page build passed. Original dirty
state retained in the named sync stash, whose SHA is in the external plan folder.
