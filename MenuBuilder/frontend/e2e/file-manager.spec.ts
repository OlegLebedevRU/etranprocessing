import { test, expect, type Page } from "@playwright/test";

async function portal(page: Page, profile: "classic" | "l4desk", available = true, conflict = false) {
  await page.addInitScript(() => Object.defineProperty(window, "showSaveFilePicker", { value: undefined, configurable: true }));
  const calls: Array<{path: string; method: string; body: any}> = [];
  const operations = new Map<string, any>();
  const fixtures = [10, 11];
  const lease = (id: string) => `22222222-2222-4222-8222-${id.padStart(12, "0")}`;
  await page.route("**/api/**", async route => {
    const request = route.request(), path = new URL(request.url()).pathname;
    const body = request.postData() ? request.postDataJSON() : null;
    calls.push({ path, method: request.method(), body });
    let data: any = {};
    if (path === "/api/auth/me") data = { username: "fixture", org_id: 7, role_id: 3, is_superuser: false, site_mode: "both", timezone: "UTC", permissions: ["*"], session_id: "fixture-browser" };
    if (path === "/api/admin/tenants/available") data = [];
    if (path === "/api/settings/terminals") data = { items: fixtures.map(id => ({ id, device_id: id, sn: `fixture${id}`, is_active: true, address: "ул. Тестовая, 15, помещение 10" })), total_count: 2 };
    if (path.includes("/internal/v1/devices")) data = { items: fixtures.map(id => ({ id, device_id: id, sn: `fixture${id}`, device_tags: [], connection: { device_id: id, last_checked_result: true, svc_connect: true, is_svc_available: true } })), pages: 1, total: 2 };
    if (path.endsWith("/readiness")) data = { state: available ? "ready" : "incompatible", available, compatible: available, mqtt_available: true, write_available: true, capabilities: ["fs.mqtt_navigation", "fs.write_user"], server_time: new Date().toISOString(), valid_until: new Date(Date.now() + 45000).toISOString(), missing_capabilities: [] };
    if (path.endsWith("/sessions") && request.method() === "POST") {
      if (conflict) { await route.fulfill({ status: 409, json: { detail: { code: "lease_taken", scope: "console" } } }); return; }
      data = { lease_id: lease(path.split("/").at(-2)!), expires_at: new Date(Date.now() + 60000).toISOString() };
    }
    if (path.includes("/operations/22222222")) data = { id: path.split("/").pop(), kind: "session", state: "active", roots: ["C:\\", "D:\\"], entries: [] };
    if (path.endsWith("/navigation")) data = { state: "completed", entries: [{ name: "Reports", directory: true, size_bytes: 0 }, { name: "report.txt", directory: false, size_bytes: 3 }], has_more: false };
    if (path.endsWith("/signals")) data = { retry_after_sec: 0, expires_at: new Date().toISOString() };
    if (path.endsWith("/operations") && request.method() === "POST") { operations.set(body.id, body); data = { ...body, state: "created" }; }
    if (path.includes("/operations/") && !path.includes("/operations/22222222")) data = { ...(operations.get(path.split("/").pop()!) || {}), state: "completed", entries: [] };
    await route.fulfill({ json: data });
  });
  await page.goto(`/files?profile=${profile}`);
  await expect(page.getByRole("tree").getByText("10", { exact: true })).toBeVisible();
  return calls;
}
async function openDisk(page: Page, id = 10) {
  await page.getByRole("tree").getByText(String(id), { exact: true }).click();
  await expect(page.getByText("Монопольный сеанс", { exact: true })).toBeVisible();
  await page.getByRole("tree").getByText("C:\\", { exact: true }).click();
  await expect(page.getByText("report.txt", { exact: true })).toBeVisible();
}

for (const profile of ["classic", "l4desk"] as const) test(`Explorer starts at fleet home without a lease in ${profile}`, async ({ page }) => {
  const calls = await portal(page, profile);
  await expect(page.getByRole("tree").getByText("Обзор парка")).toBeVisible();
  expect(calls.some(call => call.method === "POST")).toBe(false);
  await openDisk(page);
  expect(calls.some(call => call.path.endsWith("/navigation"))).toBe(true);
  expect(calls.some(call => call.path.endsWith("/operations") && call.body?.kind === "list")).toBe(false);
  await page.getByRole("row", { name: /Reports/ }).dblclick();
  await expect(page.getByRole("textbox", { name: "Путь к каталогу" })).toHaveValue("C:\\Reports");
  await page.getByRole("button", { name: "Вверх", exact: true }).click();
  await expect(page.getByRole("textbox", { name: "Путь к каталогу" })).toHaveValue("C:\\");
  const loads = calls.filter(call => call.path === "/api/settings/terminals").length;
  await page.getByRole("tree").getByText("Обзор парка").click();
  await expect(page.getByText("Монопольный сеанс", { exact: true })).toHaveCount(0);
  await expect.poll(() => calls.filter(call => call.path === "/api/settings/terminals").length).toBeGreaterThan(loads);
  expect(calls.filter(call => call.body?.action === "stop")).toHaveLength(1);
  expect(calls.filter(call => call.path.endsWith("/sessions") && call.method === "POST")).toHaveLength(1);
});

test("incompatible agent is inspected without acquiring a lease", async ({ page }) => {
  const calls = await portal(page, "classic", false);
  await page.getByRole("tree").getByText("10", { exact: true }).click();
  await expect(page.getByRole("alert")).toContainText("Требуется обновление агента");
  expect(calls.some(call => call.method === "POST")).toBe(false);
});

test("console conflict never creates a file operation", async ({ page }) => {
  const calls = await portal(page, "l4desk", true, true);
  await page.getByRole("tree").getByText("10", { exact: true }).click();
  await expect(page.getByRole("alert")).toContainText("Терминал занят: консоль");
  expect(calls.some(call => call.path.endsWith("/navigation"))).toBe(false);
});

test("terminal switch waits for previous close and drain", async ({ page }) => {
  const calls = await portal(page, "l4desk");
  await openDisk(page);
  await page.route("**/api/file-manager/v1/devices/10/sessions/*/signals", async route => {
    calls.push({ path: "close10", method: "POST", body: route.request().postDataJSON() });
    await route.fulfill({ json: { retry_after_sec: 2, expires_at: new Date().toISOString() } });
  });
  await page.getByRole("tree").getByText("11", { exact: true }).click();
  await expect(page.getByRole("status")).toContainText("Завершаем сеанс");
  expect(calls.some(call => call.path.endsWith("/devices/11/sessions"))).toBe(false);
  await expect(page.getByText("Монопольный сеанс", { exact: true })).toBeVisible({ timeout: 5000 });
  expect(calls.findIndex(call => call.path === "close10")).toBeLessThan(calls.findIndex(call => call.path.endsWith("/devices/11/sessions")));
});

test("domain navigation failure is preserved, not replaced with connection lost", async ({ page }) => {
  await portal(page, "classic"); await openDisk(page);
  await page.route("**/navigation", route => route.fulfill({ json: { state: "failed", error_code: "fm_path_or_session_failed" } }));
  await page.getByRole("row", { name: /Reports/ }).dblclick();
  await expect(page.getByRole("alert")).toContainText("Не удалось открыть путь");
  await expect(page.getByText("Связь потеряна", { exact: false })).toHaveCount(0);
});

test("modal transfer blocks navigation and corrupt S3 data closes exactly once", async ({ page }) => {
  const calls = await portal(page, "classic"); await openDisk(page);
  let release: () => void = () => {};
  const pending = new Promise<void>(resolve => { release = resolve; });
  let storageHeaders: Record<string, string> = {};
  await page.route("https://storage.example.invalid/**", async route => {
    storageHeaders = await route.request().allHeaders(); await pending;
    await route.fulfill({ body: "bad", headers: { "Access-Control-Allow-Origin": "*" } });
  });
  await page.route("**/api/file-manager/v1/devices/10/operations/**", async route => {
    const path = new URL(route.request().url()).pathname;
    if (path.includes("22222222")) { await route.fallback(); return; }
    if (path.endsWith("/download")) { await route.fulfill({ json: { url: "https://storage.example.invalid/object", headers: {}, size_bytes: 3, sha256: "0".repeat(64) } }); return; }
    await route.fulfill({ json: { state: "verifying", size_bytes: 3, sha256: "0".repeat(64) } });
  });
  await page.getByRole("button", { name: "Скачать", exact: false }).click();
  await expect(page.getByRole("dialog")).toBeVisible();
  await expect(page.getByRole("button", { name: "Вверх", exact: true })).toBeDisabled();
  expect(calls.filter(call => call.path.endsWith("/sessions") && call.method === "POST")).toHaveLength(1);
  release();
  await expect(page.getByRole("dialog")).toContainText("Операция остановлена");
  expect(storageHeaders.authorization).toBeUndefined(); expect(storageHeaders.cookie).toBeUndefined();
  expect(calls.filter(call => call.body?.action === "cancel")).toHaveLength(1);
  expect(calls.filter(call => call.body?.action === "stop")).toHaveLength(0);
  await page.getByRole("button", { name: "Понятно" }).click();
  await expect(page.getByRole("dialog")).toHaveCount(0);
});


test("save picker cancellation sends no transfer and keeps session usable", async ({ page }) => {
  const calls = await portal(page, "l4desk"); await openDisk(page);
  await expect(page.getByRole("row", { name: /Reports/ })).toHaveCSS("cursor", "pointer");
  await page.evaluate(() => Object.defineProperty(window, "showSaveFilePicker", { configurable: true, value: async () => { throw new DOMException("Cancelled", "AbortError"); } }));
  await page.getByRole("button", { name: "Скачать", exact: false }).click();
  await expect(page.getByRole("dialog")).toHaveCount(0);
  await expect(page.getByRole("status")).toHaveText("Скачивание отменено");
  expect(calls.some(call => call.path.endsWith("/operations") && call.method === "POST")).toBe(false);
  await expect(page.getByText("Монопольный сеанс", { exact: true })).toBeVisible();
});


for (const mode of ["picker", "blocked", "unsupported"] as const) test(`verified download saves with ${mode}`, async ({ page }) => {
  await portal(page, "l4desk"); await openDisk(page);
  await page.evaluate(mode => {
    const state = window as any;
    state.saved = ""; state.saveClosed = false;
    if (mode === "picker") Object.defineProperty(window, "showSaveFilePicker", { configurable: true, value: async () => ({ createWritable: async () => ({ write: async (blob: Blob) => { state.saved = await blob.text(); }, close: async () => { state.saveClosed = true; }, abort: async () => {} }) }) });
    if (mode === "blocked") Object.defineProperty(window, "showSaveFilePicker", { configurable: true, value: async () => { throw new DOMException("Blocked", "SecurityError"); } });
  }, mode);
  const sha = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
  await page.route("https://storage.example.invalid/**", route => route.fulfill({ body: "abc", headers: { "Access-Control-Allow-Origin": "*" } }));
  await page.route("**/api/file-manager/v1/devices/10/operations/**", async route => {
    const path = new URL(route.request().url()).pathname;
    if (path.includes("22222222")) { await route.fallback(); return; }
    if (path.endsWith("/download")) { await route.fulfill({ json: { url: "https://storage.example.invalid/object", headers: {}, size_bytes: 3, sha256: sha } }); return; }
    await route.fulfill({ json: { state: "verifying", size_bytes: 3, sha256: sha } });
  });
  const download = mode === "picker" ? undefined : page.waitForEvent("download");
  await page.getByRole("button", { name: "Скачать", exact: false }).click();
  await expect(page.getByRole("status")).toHaveText("Файл проверен и передан");
  await expect(page.getByRole("dialog")).toHaveCount(0);
  if (mode === "picker") expect(await page.evaluate(() => ({ saved: (window as any).saved, closed: (window as any).saveClosed }))).toEqual({ saved: "abc", closed: true });
  else expect((await download!).suggestedFilename()).toBe("report.txt");
});
