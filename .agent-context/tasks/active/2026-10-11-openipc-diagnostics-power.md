# OpenIPC diagnostics and camera power release

User authorized camera identity/freshness/time diagnostics and EN_1_12V
on/off/restart. Transaction explicitly confirmed: RES plus mandatory gauge84;
execution ESP32 → its TCA6408A P3, no STM32. Fixed restart off interval3000ms.
Preserve camera isolation, switch networking, relay ownership and other GPIO.
No five-minute run; video confirmation is the operator's explicit Есть/Нет.

Canonical IoT PR114 merged master5861964e718f4502451250c6f5c8f18703ef2490:
strict RPC164, expected_power_on, explicit disruptive consent and idle guard.
Windows643 tests passed7 skipped; contract64 passed; Ruff checks passed.
Builder Linux tests and registry publication passed. App1 was deployed through
the standard serialized deployer by immutable digest, with revision/image/HTTP
verification:
`sha256:3819d1e983ad7db1cc2dc2d2d0effcf78cadf73ae19ce9f88f2830889caaec57`.
No migrations; no neighboring services were intentionally restarted.

Siplite PR42 implements fixed output readback/direction and preserved hardware
bits, bounded version text compatibility, signed camera clock offset against
ESP UTC,330s consumer freshness and separate config/probe timestamps.
Supply voltage and camera readiness are not inferred from the output latch.
Unknown/unavailable/ambiguous results are explicit. Once restart may have
switched off, restoration to on is attempted even during shutdown/deadline.

Actual C adapter/GPIO tests and3 age tests passed; full IDF5.5.2 ccache build
firmware0018 succeeded. BIN1232736/ELF13153864 bytes, SHA256
cc2ee5dc8e0f2f808dd44ef36530567a2f37425c44e1b9b933166412f9e1c900.
Temporary HTTP staging download matched the hash; both temporary binary copies
were removed after successful OTA. Both devices booted0018: OTA_OK/HELLO event
pairs4619=1358943/1358945,4623=1358940/1358941.

Live4619 RPC160 confirms OpenIPC2.4.11.27-lite and Majestic
`master+51af9af, 2024-11-27`. Camera clock is skewed against ESP UTC; no clock
write is performed. Unknown probe values stay separate from verified power.
RPC164 dry-run, expected-state conflict, consent, short deadline and already-on
unchanged checks passed. A briefly owned active stream blocked mutation with
409 busy; only the test's stream/lease was stopped and released.

Off/on/restart each returned200 plus matching gauge84. Restart task
01168220-d0ea-49d2-8f0f-333c2e949d88 applied power.restart. Subsequent RPC160
task3e129af6-c644-4771-a1e0-3aa574b3c868 returned ready with uptime59s and
Majestic10s, confirming reboot. Original HD1280x720/15FPS/1024kbps and Day
were preserved. Operator stream confirmation is pending; no five-minute run.
4623 remains switch mode with no attached camera and received no power write.

Only Compose's app1 digest and this handoff are changed in etranprocessing.
Concurrent ProcessingBackend source/workspace changes remain outside scope.
