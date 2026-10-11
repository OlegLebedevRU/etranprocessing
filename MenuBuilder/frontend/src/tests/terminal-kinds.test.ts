import { describe, expect, it } from "vitest";
import { terminalKindOptions, terminalKindPayload } from "../utils/terminalKinds";

describe("terminal creation profiles", () => {
  it("maps Camera OpenIPC to the ESP32 platform and explicit camera driver", () => {
    expect(terminalKindOptions.find(option => option.value === "camera-openipc")?.label).toBe("Camera OpenIPC");
    expect(terminalKindPayload("camera-openipc")).toEqual({ sys: "esp32", camera: "openipc" });
  });
  it("keeps ordinary platforms separate and rejects unknown profiles", () => {
    for (const sys of ["windows", "linux", "esp32"]) expect(terminalKindPayload(sys)).toEqual({ sys });
    expect(() => terminalKindPayload("other")).toThrow();
  });
});
