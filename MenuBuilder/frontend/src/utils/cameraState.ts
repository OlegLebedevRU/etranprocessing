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
  return !!camera?.config && !camera.stale && observationAge(snapshot?.observed_at, true, now,
    snapshot?.freshness_max_age_sec ?? 330) === "Актуально";
}
export function powerFresh(snapshot: CameraSnapshot | null, camera?: CameraState, now = Date.now()): boolean {
  return typeof camera?.power?.enabled === "boolean" && observationAge(camera.power.observed_at,
    true, now, snapshot?.freshness_max_age_sec ?? 330) === "Актуально";
}
