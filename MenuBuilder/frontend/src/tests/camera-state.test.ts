import { expect, it } from "vitest";
import { configFresh, isOpenIpcCamera, observationAge, powerFresh } from "../utils/cameraState";

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
