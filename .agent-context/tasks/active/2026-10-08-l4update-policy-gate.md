# Remote production-policy readiness gate

Intake: native l4setup/l4superv remote communication readiness. User requires
normal Leo4Proxy/policy checks in addition to REQ/RSP + fresh EVT/EVA. Production
SCM/installed files, broker connections, helper, MB/PB are outside this patch.

`l4common/proxy_policy_gate.h` validates the existing normal `/_leo4/info` policy
object: both MQTT/RTP and outgoing HTTPS permission, persisted known grant
(last_success_at>0, nonzero generation, storage_pending=false), exact existing
72-hour grant deadline, now within it, empty stop_facts and typed last_error.
Unknown initial grace and denied/expired/unavailable policies fail. A known cached
grant remains valid despite a later network error, per the existing policy window.
No new endpoint or modification to policy permissions is introduced.

`setup_proxy_probe_policy` separately checks ready/certificate/private key/routes,
nonempty SN, valid selected thumbprint and explicit expected certificate when
present. Generic/candidate `setup_proxy_probe` semantics remain unchanged.
Production readiness and certificate capture hold/query the original process and
repeat creation epoch, SCM command/PID and HTTP/MQTT listener ownership around
HTTP policy validation. Supervisor normal info_parse uses the same policy gate;
existing expected SN/thumbprint/listener checks remain mandatory.

Targeted MSVC /MT x86/x64 tests passed: supervisor communication signals698/0 each
(actual loopback HTTP/TCP/PID ownership, modeled SCM/Con IPC); readiness adapter
both architectures (actual pipes/TCP, modeled SCM/identity responses); proxy HTTP
and pure policy expired/denied/unavailable/future/identity checks. First test
compile caught missing _countof definition in fixture; corrected to sizeof array.
No service/broker/IoT mutation, signing or deployment. Root coordinates full
production unified builds with fresh-install work to avoid object collisions.
