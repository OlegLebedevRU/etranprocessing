import { beforeEach, describe, expect, it, vi } from "vitest";
import client from "../api/client";
import { triggerDeviceProvisioning } from "../api/devices";

vi.mock("../api/client", () => ({
  default: { get: vi.fn(), post: vi.fn() },
}));

const terminal = { id: 1, device_id: 773, org_id: 1, sn: "terminal-773" };

describe("device passport provisioning", () => {
  beforeEach(() => vi.resetAllMocks());

  it("resolves device number to internal ID and uses the existing admin route", async () => {
    vi.mocked(client.get).mockResolvedValue({ data: { items: [terminal] } });
    vi.mocked(client.post).mockResolvedValue({ data: { success: true } });

    await expect(triggerDeviceProvisioning(1, 773, "terminal-773"))
      .resolves.toEqual({ success: true });
    expect(client.get).toHaveBeenCalledWith("/admin/terminals", {
      params: { org_id: 1, search: "773", page_size: 100 },
    });
    expect(client.post).toHaveBeenCalledExactlyOnceWith("/admin/terminals/1/provision-iot");
  });

  it.each([
    { items: [] },
    { items: [{ ...terminal, org_id: 2 }] },
    { items: [{ ...terminal, device_id: 774 }] },
    { items: [{ ...terminal, sn: "different-terminal" }] },
    { items: [terminal, { ...terminal, id: 2 }] },
  ])("does not provision when identity lookup is missing, mismatched or ambiguous: %j", async ({ items }) => {
    vi.mocked(client.get).mockResolvedValue({ data: { items } });
    await expect(triggerDeviceProvisioning(1, 773, "terminal-773")).rejects.toThrow(
      "Не удалось однозначно определить запись терминала для провиженинга"
    );
    expect(client.post).not.toHaveBeenCalled();
  });

  it("does not provision after an unauthorized lookup", async () => {
    vi.mocked(client.get).mockRejectedValue(new Error("Forbidden"));
    await expect(triggerDeviceProvisioning(1, 773, "terminal-773")).rejects.toThrow("Forbidden");
    expect(client.post).not.toHaveBeenCalled();
  });
});
