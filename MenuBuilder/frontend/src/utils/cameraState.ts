import type { DeviceListItem } from "../api/devices";
import type { CameraSnapshot, CameraState } from "../api/cameras";

export function isOpenIpcCamera(device: Pick<DeviceListItem, "tags">): boolean {
  return device.tags.some(tag => tag.tag === "camera" && tag.value === "openipc");
}
export function observationAge(observed: string | undefined, valid = true, now = Date.now(), limit = 330): string {
  if (!valid || !observed || !/(Z|[+-]\d\d:\d\d)$/.test(observed)) return "Неизвестно";
  const time = Date.parse(observed);
  if (!Number.isFinite(time) || !Number.isFinite(limit) || limit < 1 || limit > 3600) return "Неизвестно";
  const age = (now - time) / 1000;
  if (age < -5) return "Некорректное время";
  return age > limit ? "Устарело" : "Актуально";
}
export function configFresh(snapshot: CameraSnapshot | null, camera?: CameraState, now = Date.now()): boolean {
  return !!camera?.config && !camera.stale && observationAge(snapshot?.received_at ?? undefined, true, now,
    snapshot?.freshness_max_age_sec ?? 330) === "Актуально";
}
export function powerFresh(snapshot: CameraSnapshot | null, camera?: CameraState, now = Date.now()): boolean {
  return typeof camera?.power?.enabled === "boolean" && observationAge(camera.power.observed_at,
    true, now, snapshot?.freshness_max_age_sec ?? 330) === "Актуально";
}

export function cameraVideoSettings(camera?: CameraState): string {
  const video = camera?.config?.video0;
  if (!video) return "Неизвестно";
  const size = video.size === "" ? "Native" : video.size ?? "Размер неизвестен";
  const fps = video.fps === 0 ? "FPS: Auto" : video.fps === undefined ? "FPS неизвестен" : `${video.fps} FPS`;
  const bitrate = video.bitrate === 0 ? "Битрейт: Auto" : video.bitrate === undefined ? "Битрейт неизвестен" : `${video.bitrate} кбит/с`;
  return [video.codec === "h265" ? "H.265" : video.codec === "h264" ? "H.264" : null, size, fps, bitrate].filter(Boolean).join(" · ");
}

export function cameraObservedFormat(snapshot: CameraSnapshot | null, camera?: CameraState, now = Date.now()): string {
  const track = camera?.actual_track;
  if (!track || camera?.stale || observationAge(snapshot?.observed_at, true, now,
    Math.min(snapshot?.freshness_max_age_sec ?? 60, 60)) !== "Актуально") return "Нет актуальной пробы";
  return [track.codec === "h265" ? "H.265" : "H.264", `${track.width}×${track.height}`, `${track.fps} FPS`,
    track.codec === "h265" && track.level_id !== undefined ? `Level ${track.level_id / 30}` : null,
    track.bit_depth !== undefined ? `${track.bit_depth}-bit` : null, track.chroma ?? null].filter(Boolean).join(" · ");
}
