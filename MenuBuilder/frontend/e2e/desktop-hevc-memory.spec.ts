import { expect, test, type Page } from "@playwright/test";

async function desktop(page: Page, busy = false) {
  const writes: string[] = [];
  const capabilityReads: string[] = [];
  await page.route("**/api/**", async route => {
    const request = route.request();
    const url = new URL(request.url());
    const path = url.pathname;
    if (request.method() !== "GET") writes.push(path);
    let data: any = {};
    if (path === "/api/auth/me") data = {
      username: "fixture", org_id: 1, role_id: 3, role: "user",
      site_mode: "classic", permissions: ["video:view"], timezone: "UTC",
    };
    if (path.includes("/internal/v1/devices")) data = {
      items: [{ device_id: 773, sn: "hevc-fixture", connection: { last_checked_result: true },
        device_tags: [{ tag: "sys", value: "windows" }] }],
      pages: 1, total: 1, stats: {},
    };
    if (path === "/api/settings/terminals") data = {
      items: [{ id: 1, device_id: 773, sn: "hevc-fixture", is_active: true }], total_count: 1,
    };
    if (path.endsWith("/control/status")) data = { agent: { online: true }, lease: { active: false } };
    if (path.endsWith("/inventory")) {
      data = { displays: [{ id: "disp:fixture", desktop_id: "disp:fixture", name: "DISPLAY1",
        width: 1920, height: 1080, primary: true, policy: "input" }], cameras: [] };
      if (url.searchParams.get("contract_version") === "2") {
        capabilityReads.push(url.searchParams.get("source_id") || "");
        data.video_codecs = { hevc_available: !busy, reason_code: busy ? "encoder_busy" : "ready" };
      }
    }
    await route.fulfill({ json: data });
  });
  await page.goto("/video?device_id=773");
  await page.getByRole("button", { name: "Настройки видео", exact: true }).click();
  await expect(page.getByRole("combobox", { name: "Кодек общего потока" })).toBeVisible();
  return { writes, capabilityReads };
}

test("remembered HEVC remains selectable after inventory refresh without stream commands", async ({ page }) => {
  const observed = await desktop(page);
  await expect.poll(() => observed.capabilityReads.length).toBeGreaterThan(0);
  await expect(page.getByText("H.265 недоступен:", { exact: false })).toHaveCount(0);
  const before = observed.capabilityReads.length;
  await page.getByRole("button", { name: /Обновить источники и статус/ }).click();
  await expect.poll(() => observed.capabilityReads.length).toBeGreaterThan(before);
  await expect(page.getByText("H.265 недоступен:", { exact: false })).toHaveCount(0);
  expect(observed.capabilityReads.every(source => source === "disp:fixture")).toBe(true);
  expect(observed.writes).toEqual([]);
});

test("unknown busy is a deferred check rather than absence of HEVC", async ({ page }) => {
  const observed = await desktop(page, true);
  await expect(page.getByText("Кодировщик занят, проверка H.265 отложена", { exact: true })).toBeVisible();
  await expect(page.getByText("H.265 недоступен: encoder_busy", { exact: true })).toHaveCount(0);
  expect(observed.writes).toEqual([]);
});
