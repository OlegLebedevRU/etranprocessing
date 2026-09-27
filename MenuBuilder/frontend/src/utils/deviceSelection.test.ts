import { describe, expect, it } from "vitest";
import { linkedDevice } from "./deviceSelection";

describe("tenant-owned terminal deep links", () => {
  const items = [{device_id: 773, sn: null}, {device_id: 774, sn: "partial"}];
  it("selects an ID without requiring a complete SN", () => {
    expect(linkedDevice(items, new URLSearchParams("device_id=773"))).toBe(items[0]);
    expect(linkedDevice(items, new URLSearchParams("device_id=774"))).toBe(items[1]);
  });
  it("does not fall back to another device for a foreign or invalid ID", () => {
    for (const id of ["999", "NaN", "", "-1"]) {
      expect(linkedDevice(items, new URLSearchParams({device_id:id, sn:"partial"}))).toBeNull();
    }
  });
  it("preserves old SN links and distinguishes an absent link", () => {
    expect(linkedDevice(items, new URLSearchParams("sn=partial"))).toBe(items[1]);
    expect(linkedDevice(items, new URLSearchParams("sn="))).toBeNull();
    expect(linkedDevice(items, new URLSearchParams())).toBeUndefined();
  });
});
