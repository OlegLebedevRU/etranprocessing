import { expect, it } from "vitest";
import { cameraObservedFormat, cameraVideoSettings, configFresh, isOpenIpcCamera, observationAge, powerFresh } from "../utils/cameraState";
import { cameraPatch, editableCameraFields } from "../utils/cameraDraft";

it("uses explicit camera tag independently of platform and connectivity", () => {
  expect(isOpenIpcCamera({ tags: [{ tag: "sys", value: "ESP32" }] })).toBe(false);
  expect(isOpenIpcCamera({ tags: [{ tag: "camera", value: "openipc" }] })).toBe(true);
  expect(isOpenIpcCamera({ tags: [{ tag: "camera", value: "other" }] })).toBe(false);
});
it("ages each observation and rejects missing or future clocks", () => {
  const now = Date.parse("2026-10-11T20:00:00Z");
  expect(observationAge("2026-10-11T19:55:00Z", true, now)).toBe("Актуально");
  expect(observationAge("2026-10-11T19:54:00Z", true, now)).toBe("Устарело");
  expect(observationAge("2026-10-11T20:00:20Z", true, now)).toBe("Некорректное время");
  expect(observationAge("2026-10-11T20:00:00", true, now)).toBe("Неизвестно");
  expect(observationAge("2026-10-11T20:00:00Z", false, now)).toBe("Неизвестно");
});
it("verified power does not make unknown camera configuration writable", () => {
  const now = Date.parse("2026-10-11T20:00:00Z");
  const camera = { camera_id: "cam:ip000001", power: { enabled: true, observed_at: "2026-10-11T20:00:00Z" } };
  const snapshot = { snapshot_id: "fixture", observed_at: "2026-10-11T20:00:00Z", cameras: [camera] };
  expect(powerFresh(snapshot, camera, now)).toBe(true);
  expect(configFresh(snapshot, camera, now)).toBe(false);
});

it("uses server receipt for configuration without requiring the device UTC clock", () => {
  const received_at = "2026-10-11T20:00:00Z";
  const now = Date.parse(received_at);
  const camera = { camera_id: "cam:ip000001", config: { video0: { size: "" } } };
  const snapshot = { snapshot_id: "fixture", received_at, cameras: [camera] };
  expect(configFresh(snapshot, camera, now)).toBe(true);
  expect(configFresh(snapshot, camera, now + 331000)).toBe(false);
  expect(configFresh({ ...snapshot, received_at: null, observed_at: received_at }, camera, now)).toBe(false);
  expect(configFresh(snapshot, { ...camera, stale: true }, now)).toBe(false);
});

it("edits native luminance when advertised without requiring unrelated capabilities", () => {
  const camera = { camera_id: "cam:ip000001", capabilities: { fields: [{ path: "image.luminance", type: "integer" as const,
    read: true, write: true, disruptive: false, reason: null, minimum: 0, maximum: 100, values: [] }] } };
  const fields = editableCameraFields(camera);
  expect(fields.map(field => field.path)).toEqual(["image.luminance"]);
  expect(cameraPatch({ "image.luminance": 55 }, fields)).toEqual({ image: { luminance: 55 } });
});

it("keeps Majestic automatic settings distinct from a concrete native SPS probe", () => {
  const now = Date.parse("2026-10-24T00:00:00Z");
  const camera = { camera_id: "cam:ip000001", config: { video0: { codec: "h265", size: "", fps: 0, bitrate: 0 } },
    actual_track: { codec: "h265" as const, width: 2592, height: 1944, fps: 20, level_id: 153, bit_depth: 8, chroma: "420" } };
  const snapshot = { snapshot_id: "fixture", observed_at: "2026-10-24T00:00:00Z", cameras: [camera] };
  expect(cameraVideoSettings(camera)).toBe("H.265 · Native · FPS: Auto · Битрейт: Auto");
  expect(cameraObservedFormat(snapshot, camera, now)).toBe("H.265 · 2592×1944 · 20 FPS · Level 5.1 · 8-bit · 420");
  expect(camera.config.video0).toEqual({ codec: "h265", size: "", fps: 0, bitrate: 0 });
  expect(cameraObservedFormat(snapshot, camera, now + 61000)).toBe("Нет актуальной пробы");
  expect(cameraVideoSettings({ camera_id: "cam:ip000001", config: { video0: {} } })).not.toContain("Auto");
});
