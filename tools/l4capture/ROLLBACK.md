# l4capture — rollback / install notes (suite 1.8.0)

## Install path
- `C:\l4tools\l4capture\bin\l4capture.exe` (matches l4desk adapter `%BASE%\l4capture\bin`).
- Companion files in `C:\l4tools\l4capture\`: `OPENH264_LICENSE.txt`, `NOTICE-OpenH264.txt`, `SBOM.json`, this guide.

## Enable desktop backend
- l4desk config `media_backend=l4capture` (default in current config).
- Fallback: `media_backend=ffmpeg` (cameras always use FFmpeg).
- l4capture is **not** a service: only l4desk starts it in an interactive session under a valid lease.

## Upgrade
1. Stop active stream via normal lifecycle (l4desk / MenuBuilder).
2. Confirm child `l4capture.exe` exited and input is released.
3. Run `l4setup.exe` (or replace `l4capture\bin\l4capture.exe` with matching arch).
4. Do not copy x86 binary into x64 payload.

## Rollback
1. Stop stream / ensure no `l4capture.exe` process.
2. Restore previous `l4capture\bin\l4capture.exe` (or reinstall previous suite package).
3. Restart l4desk. **Do not** auto-fallback to FFmpeg on lease expiry/lock — operator starts desktop capture again.
4. Camera streams remain on FFmpeg across upgrade/rollback.

## Privacy
- Local log `l4capture.log` (5 MiB x2) is diagnostics only; scrubbed for pin/token/password/secret.
- Never ship terminal private keys, PIN material, or screen dumps in the package.
