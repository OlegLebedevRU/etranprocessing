export function browserVideoCodecs() {
  const checked_at_ms = Date.now();
  let h264: boolean | null = null;
  let h265: boolean | null = null;
  try {
    const capabilities = RTCRtpReceiver.getCapabilities("video");
    if (capabilities && Array.isArray(capabilities.codecs)) {
      const codecs = capabilities.codecs.map(codec => codec.mimeType.toLowerCase());
      h264 = codecs.includes("video/h264");
      h265 = codecs.includes("video/h265");
    }
  } catch { /* Unknown capability cannot authorize Auto fallback. */ }
  return { h264, h265, checked_at_ms, valid_until_ms: checked_at_ms + 60000 };
}
