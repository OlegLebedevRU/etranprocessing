import { describe, expect, it, vi } from "vitest";
import { renderToStaticMarkup } from "react-dom/server";
import { SourceSelector } from "../components/video/SourceSelector";
import { createVideoSession, getDeviceInventory, getDeviceStreamState, getVideoWatchSnapshot, startDeviceStream } from "../api/video";
import client from "../api/client";
import { qualityReadDue } from "../hooks/useDesktopQuality";

vi.mock("../api/client", () => ({ default: { get: vi.fn(), post: vi.fn() } }));

describe("media v3 quality policies", () => {
  it("selects v3 on inventory, state and viewer session requests", async () => {
    vi.mocked(client.get).mockResolvedValue({ data: {} });
    vi.mocked(client.post).mockResolvedValue({ data: {} });
    await getDeviceInventory(1, false, { source_id: "disp:primary", profile_id: "low" });
    expect(client.get).toHaveBeenCalledWith("/v1/video/devices/1/inventory", {
      params: { refresh: 0, contract_version: 3, source_id: "disp:primary", profile_id: "low" },
      timeout: 45000,
    });
    await getDeviceStreamState(1);
    expect(client.get).toHaveBeenCalledWith("/v1/video/devices/1/stream/state", { params: { contract_version: 3 } });
    await createVideoSession(1, 3);
    expect(client.post).toHaveBeenCalledWith("/v1/video/devices/1/session", undefined, { params: { contract_version: 3 } });
  });

  it("coalesces continuous quality reports and still reads after commands and silence", () => {
    let lastRead = 0;
    const reads = [];
    for (let ms = 1000; ms <= 60000; ms += 1000) {
      if (qualityReadDue(ms - lastRead, true, false)) {
        reads.push(ms);
        lastRead = ms;
      }
    }
    expect(reads).toHaveLength(12);
    expect(qualityReadDue(1000, false, true)).toBe(true);
    expect(qualityReadDue(14999, false, false)).toBe(false);
    expect(qualityReadDue(15000, false, false)).toBe(true);
  });

  it("reads agent presence and stream state in one watch request", async () => {
    vi.mocked(client.get).mockClear();
    const snapshot = { agent: { online: true }, stream: { state: "running" } };
    vi.mocked(client.get).mockResolvedValueOnce({ data: snapshot });
    expect(await getVideoWatchSnapshot(773, false)).toEqual(snapshot);
    expect(client.get).toHaveBeenCalledTimes(1);
    expect(client.get).toHaveBeenCalledWith("/v1/video/devices/773/stream/state", { params: { contract_version: 3, watch: true } });
  });

  for (const profile of ["low", "default"]) {
    for (const [width, height] of [[800, 600], [1024, 768], [1280, 1024], [1920, 1080], [3000, 2000]]) {
      it(`${profile} preserves the ${width}x${height} descriptor without scaling`, async () => {
        const descriptor = { contract_version: 3, source_id: "disp:primary", profile_id: profile,
          tracks: [{ codec: "h265", width, height, fps: profile === "low" ? 10 : 15, level_id: 150 }] };
        vi.mocked(client.post).mockResolvedValue({ data: { descriptor } });
        const payload = { mode: "desktop" as const, source_id: "disp:primary", profile, codec_policy: "h265" as const };
        const response = await startDeviceStream(1, payload);
        expect(response.descriptor).toEqual(descriptor);
        expect(client.post).toHaveBeenLastCalledWith("/v1/video/devices/1/stream/start", payload, { _retry: true, timeout: 120000 });
      });
    }
  }

  it("explains the two quality policies without promising a raster", () => {
    const markup = renderToStaticMarkup(<SourceSelector inventory={null} selectedSourceKey=""
      onSelectSourceKey={() => {}} selectedProfile="low" onChangeProfile={() => {}}
      loadingInventory={false} onRefreshInventory={() => {}} />);
    expect(markup).toContain("Medium — умеренный трафик; HD — выше детализация. Формат задаёт источник.");
    expect(markup).not.toContain("720p");
    expect(markup).not.toContain("1080p");
  });
});
