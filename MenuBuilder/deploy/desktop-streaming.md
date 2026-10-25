# Desktop streaming configuration

`desktop-streaming.compose.yaml` is an explicit opt-in overlay for the existing
MenuBuilder and app1 services. It is not applied by a normal image update.
Include it in the reviewed release configuration together with every currently
active operator overlay and the verified image pins. Preserve that complete
file list in subsequent releases and rollback metadata.

Before application, verify the media service's readiness and the terminal-local
proxy RTP/RTCP listeners. The overlay defaults are5004/5005. Override
`VIDEO_PROXY_RTP_PORT` and `VIDEO_PROXY_RTCP_PORT` through the deployment
environment if those listeners use different ports. These are not Janus ports
and do not use `VIDEO_PORT_BASE` or per-device mountpoint allocation.

The overlay enables desktop descriptor negotiation in MenuBuilder and
`APP_CONFIG__REMOTE_INPUT__VIDEO_V2_ENABLED` /
`APP_CONFIG__REMOTE_INPUT__VIDEO_HEVC_CHAIN_READY` in app1. It does not enable
camera firmware/admission or change broker/Nginx/signer/ProcessingBackend.
Do not enable chain-ready based only on a container's running state.

After deployment, inspect the effective settings inside each running service:

- MenuBuilder: `video_contract_v2_enabled=true`, session orchestration enabled,
  nonzero matching `video_proxy_rtp_port` and `video_proxy_rtcp_port`.
- app1: `remote_input.video_v2_enabled=true` and
  `remote_input.video_hevc_chain_ready=true`.
- Container revision/digest, health, complete Compose file list and unchanged
  neighboring containers. Recreate only the approved services; signer may share
  the MenuBuilder image but is not part of this correction.

Code compatibility and live acceptance are separate. Old agents are assessed by
the retained CTL-v1 producer contract and mocked route/HTTP tests. The final
acceptance gate is an owner-visible browser on773 after the agreed deployment:
H.264/H.265/Auto, Medium/HD, fresh decoded frames, keepalive and explicit STOP.
Never report live acceptance from a mocked READY or HTTP200 alone.
