# Camera control capability audit

The camera contracts describe the protocol surface, not a requirement that every
device implement every operation. Admission of one operation checks that operation
and its requested fields. Missing illumination, power or day/night capabilities
must not prevent video settings, discovery, viewing or independent IR-cut diagnostics.

## Current boundaries

| Flow | Required evidence | Independent of |
|---|---|---|
| Video/image patch (161) | Settings revision, advertised writable fields, idle source, online main_app | Power/night/illumination support, media enrollment |
| Discovery/binding (165/166) | Relevant method; binding additionally checks discovery generation and port-2 candidate | Native API support for discovering a candidate |
| Configured IR-cut diagnostic (169) | Its own IR-cut revision and inspection; start checks idle source | Settings method list, media contract revisions, GPIO search (168) |
| Media viewing | Negotiated media/control revisions and real source observation | Camera settings writers and illumination support |

IoT writer, Native Auto and monotonic runtime flags are separate runtime switches.
Enabling their existing implementations does not advertise missing firmware
capabilities or turn a proposal document into runtime support. Existing cohort
allowlists and tenant/initiator ownership remain explicit deployment scope.

## Configuration receipt time

The BFF adds `snapshot.received_at` from the IoT gauge row's `updated_at`.
This is server receipt metadata, not an alteration of the frozen MQTT snapshot.
An HTTP read never creates a new receipt. Configuration controls use this timestamp
and preserve the camera's `stale` indication. Optional device `observed_at` does not
gate configuration writes. Source-side revision/readback validation remains required.

Receipt of a cached state does not prove a new native video probe, image change,
or browser decode. Those observations retain their own evidence and timestamps.

## Confirmed implementation gaps

The modern firmware settings snapshot advertises 160/161/165/166 and native image
fields including `image.luminance`. The UI must expose that native brightness field.
The firmware also contains legacy 162/164 handlers, but their presence alone does
not establish usable day/night or power support in the modern snapshot. The
modern wrapper replaces the legacy capability/configuration revision; day/night
requires reconciling that revision with the legacy handler before enabling it.
The UI reports these actions unavailable rather than sending unverified commands.

Generic illumination commissioning (167) retains a separate unverified physical
actuator gate. It is unrelated to the configured IR-cut diagnostic (169). No new
illumination hardware or GPIO pair is inferred from an example board.

## Verification

Backend tests cover a fresh server receipt with no device UTC, stale/malformed/
missing receipt rejection and preservation of the original gauge object. Frontend
tests cover independent luminance capability and server-receipt configuration
freshness. Browser fixtures are separate from live hardware acceptance; release
evidence records exact deployed revisions and actual commands/results.
