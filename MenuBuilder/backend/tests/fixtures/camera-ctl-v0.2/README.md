# Frozen downstream camera CTLv1 admission0.2 regression input

`camera-ctl.schema.json` is a byte-exact test mirror of IoT owner
contracts/camera-ctl-monotonic-v1/camera-ctl.schema.json, SHA256
e75e1c7f49f0d0fc68f6720a3c47766ccb4caad83d721b5d1b8aabe3d3b58a0d.
Owner accepted source: c622c7cf1c2566ca25401099a9d434e45316784d.
`start.json` is the public synthetic `messages.start` from that packet's
vectors.json; it supplies the remaining valid control/binding fields.

The regression obtains an actual BFF camera common START, copies its unchanged
operation ID to CTL command_id and publisher stream ID to stream_instance_id as
IoT producer/control do, then validates the result using the frozen schema.
The schema is not forked, relaxed or used to implement runtime behavior here.
The MEDIA session ID is not a CTL field and retains existing UUID5 mapping.
