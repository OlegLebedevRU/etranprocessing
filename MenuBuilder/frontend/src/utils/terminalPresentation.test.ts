import { describe, expect, it } from "vitest";
import type { DeviceListItem } from "../api/devices";
import type { TerminalSettingsItem } from "../api/settings";
import {
  certificatePresentation,
  filterTerminals,
  selectableDevices,
  terminalStatus,
} from "./terminalPresentation";
import {
  hasPermission,
  getDefaultRouteForViewer,
  PERMISSION_MONITORING_VIEW,
} from "./permissions";

const terminal = (id: number, active = true) =>
  ({ id, device_id: id, is_active: active }) as TerminalSettingsItem;
const device = (id: number, status: DeviceListItem["status"]) =>
  ({ device_id: id, status, connection: {} }) as DeviceListItem;

describe("terminal presentation", () => {
  it("filters the whole result before pagination and gives disabled priority over live presence", () => {
    const rows = Array.from({ length: 45 }, (_, i) => terminal(i + 1, i !== 0));
    const devices = new Map([
      [1, device(1, "online")],
      [45, device(45, "online")],
      [2, device(2, "offline")],
    ]);
    expect(
      filterTerminals(rows, devices, "online").map((item) => item.id),
    ).toEqual([45]);
    expect(
      filterTerminals(rows, devices, "disabled").map((item) => item.id),
    ).toEqual([1]);
    expect(
      filterTerminals(rows, devices, "offline").map((item) => item.id),
    ).toEqual([2]);
    expect(terminalStatus(rows[0], devices.get(1))).toBe("disabled");
    expect(terminalStatus(rows[2])).toBe("unknown");
  });
  it("excludes deleted/provider-only and disabled devices from video selection", () => {
    expect(
      selectableDevices(
        [
          device(4, "online"),
          device(2, "offline"),
          device(3, "online"),
          device(1, "online"),
        ],
        [terminal(1), terminal(2, false), terminal(4)],
      ).map((item) => item.device_id),
    ).toEqual([1, 4]);
  });
  it("distinguishes missing, unknown, expiring and expired certificates at the 30-day boundary", () => {
    const now = Date.UTC(2026, 9, 2);
    const cert = (days: number) => ({
      cert_serial: "fixture",
      cert_not_valid_after: new Date(now + days * 86400_000).toISOString(),
    });
    expect(
      certificatePresentation(
        { cert_serial: null, cert_not_valid_after: null },
        now,
      ).text,
    ).toBe("Не активирован");
    expect(
      certificatePresentation(
        { cert_serial: "fixture", cert_not_valid_after: null },
        now,
      ).text,
    ).toBe("Срок неизвестен");
    expect(certificatePresentation(cert(30), now).color).toBe("default");
    expect(certificatePresentation(cert(29), now).color).toBe("warning");
    expect(certificatePresentation(cert(0), now).color).toBe("error");
  });
  it("role 5 cannot gain monitoring via wildcard permissions and lands on terminals", () => {
    const owner = { username: "fixture", role_id: 5, permissions: ["*"] };
    expect(hasPermission(owner, PERMISSION_MONITORING_VIEW)).toBe(false);
    expect(getDefaultRouteForViewer(owner)).toBe("/terminals");
    expect(
      hasPermission({ ...owner, role_id: 3 }, PERMISSION_MONITORING_VIEW),
    ).toBe(true);
  });
});
