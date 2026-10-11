# siplite video control and UI cleanup

## Scope and source

User requested review/fixes of siplite inventory, lease, stream_start, diagnostics
buffers, and live OTA acceptance on 4619/4623. This repository's delta is limited
to MenuBuilder/frontend/src/routes/video-surveillance.tsx, in the working tree.
Device implementation and ignored acceptance evidence reside in
D:/esp32-workplace/sip. Frozen app1 ctl/lease contracts remain unchanged.

## Changes

- Accept source modes only desktop/usb-camera; presence state stopped is not a mode.
- Clean pending startup leases even before coordinator registration.
- Coordinated release stops the owned stream, then releases the lease even if stop fails.
- No changes to backend, tenant records, certificates, permissions or billing.

## Verification and release

Latest sustained acceptance uses siplite 0010 on both devices (fresh OTA_OK/HELLO:
4619 1358780/1358782, 4623 1358781/1358783). Operator-reported periodic freezes
were reproduced on 0008/0009 as ~6.1-second browser freezes. Hardware telemetry
localized loss before UDP to EMAC DMA. Camera RX copies now move to PSRAM before
lwIP queues them, with bounded 64-packet UDP capacity, 24x1536-byte internal DMA
RX buffers and RX priority 20. No upstream ctl/lease changes or further UI release
were required. Five-minute deployed browser acceptance passes with zero freezes,
RTP losses, NACK and additional PLI after startup. Independent capture: 3014 RTP
packets/279.092 seconds/50 IDRs, zero sequence gaps. Hardware DMA/FIFO loss and
PSRAM allocation fallback are zero; internal free memory stays above 50 KiB.
Camera source cadence is currently ~2.65 fps, a separate limitation. First and
repeated playback, stop and route unmount pass without HTTP/JS errors; all owned
leases are released. 4623 retains switch mode 0 and downstream 4624 answers RPC.
Full evidence: D:/esp32-workplace/sip/build/acceptance/freeze-followup-0010.json.

Six actual callback regressions, nine existing lifecycle/selection tests, and
frontend production build passed. Baseline JS/CSS/index matched production
before release. Hashed assets were added, index replaced atomically, and
methodCodes.json retained. Public index SHA256:
a6d7600ab576b4aebd6f162ec84f30aaf1baedead93df463f0ea25113a6c0c18.
Release and rollback index reside on the existing deployment host in
/home/user1/.etran-ci/video-ui-20261011-0001/ (index.before.html).

Initial short live browser acceptance passed on device 0008: 1280x720 first
and repeated playback (7/5 decoded frames at sampling), no JS or HTTP errors,
and inactive owned leases after stop and route unmount.
The camera's Majestic target was corrected to udp://192.168.77.1:62800.
4619 uses eth_port_mode=1, while 4623 must retain switch mode 0 for downstream 4624.
Device 0006 receives valid RTP but fails internal TLS allocations when UDP fills
its 64-packet mailbox. The eight-packet budget in 0007 drops IDR fragments.
0008 restores the existing external-memory TLS default in actual sdkconfig and
bounds the socket backlog at 48 MTU payloads (70656 bytes). A 240-packet capture
across three IDRs has no sequence gaps. Both 4619 and 4623 boot 0008 after fresh
OTA_OK/HELLO and pass 32-second keepalive/stop/release checks. 4623 has no camera.
Test sessions are always stopped/released in finally; 4624 answers a fresh RPC.

## Evidence and remaining checks

D:/esp32-workplace/sip/build/acceptance contains OTA_RESULT/HELLO, video RPC,
browser HTTP statuses/frame counts, and cleanup evidence. No credentials are in
this handoff. The operator confirmed live ESP_LOG display after 0010 acceptance.
Hardware Ethernet fallback was not forced again; actual teardown regression
covers stale events/address reuse and injected driver-stop failures.
Final API checks confirm both streams stopped, leases inactive and ingress
disconnected. RabbitMQ mon_tmp and the old local ctl monitors are absent.
Own OTA HTTP servers/UART capture are stopped, and remote pcap files removed.
