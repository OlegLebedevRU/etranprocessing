import type { CameraField, CameraPatch, CameraState, CameraValue } from "../api/cameras";

const allowed = new Set([
  "video0.codec", "video0.size", "video0.fps", "video0.bitrate", "video0.gopSize", "video0.rcMode",
  "image.mirror", "image.flip", "image.rotate", "image.contrast", "image.luminance", "image.saturation", "image.hue",
  "nightMode.lightMonitor",
]);

export function editableCameraFields(camera?: CameraState): CameraField[] {
  return (camera?.capabilities?.fields ?? []).filter(field => allowed.has(field.path) && field.read && field.write);
}

export function reportedCameraField(camera: CameraState | undefined, path: string): CameraValue | undefined {
  const [section, key] = path.split(".");
  return camera?.config?.[section]?.[key];
}

export function cameraPatch(values: Record<string, CameraValue>, fields: CameraField[]): CameraPatch {
  const patch: CameraPatch = {};
  for (const field of fields) {
    if (!allowed.has(field.path) || !field.write || !Object.hasOwn(values, field.path)) continue;
    const [section, key] = field.path.split(".");
    (patch[section] ??= {})[key] = values[field.path];
  }
  return patch;
}
