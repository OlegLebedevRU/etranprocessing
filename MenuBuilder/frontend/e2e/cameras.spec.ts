import { expect, test, type Page } from "@playwright/test";

async function cameraPortal(page: Page, options: { viewer?: boolean; busy?: boolean; offline?: boolean; stale?: boolean; lost?: boolean; unauthorized?: boolean; native?: boolean; revisionAfterPoll?: boolean; rejectStart?: boolean } = {}) {
  const posts: { path: string; body: any }[] = [];
  let operation = "";
  let config: any = { video0: { size: "1280x720", fps: 15, bitrate: 1024 }, nightMode: { lightMonitor: false } };
  let reads = 0;
  let power = true;
  const snapshot = () => ({ snapshot_id: "fixture-snapshot", operation_id: operation, observed_at:
    new Date(Date.now() - (options.stale ? 400000 : 0)).toISOString(), cameras: [{
      camera_id: "cam:ip000001", status: "ready", revision: (options.revisionAfterPoll && reads > 1 ? "b" : "a").repeat(64), config,
      ...(options.native ? { camera_contract_revision: "siplite-camera/1.0.0" } : {}),
      ...(options.native ? { discovery: { generation: 2, state: "ready", physical_port: 2, ethernet_mode: "switch", candidates: [
        { candidate_id: "verified", mac: "02:00:00:00:00:01", ip: "192.0.2.10", native_api: "supported", auth: "verified", port_verified: true },
        { candidate_id: "unverified", mac: "02:00:00:00:00:02", ip: "192.0.2.11", native_api: "unknown", auth: "unknown", port_verified: false },
      ] } } : {}),
      power: { enabled: power, observed_at: new Date().toISOString() },
      capabilities: { ...(options.native ? { methods: [160, 161, 165, 166], fields: [
          { path: "video0.fps", type: "integer", read: true, write: true, disruptive: true, reason: null, minimum: 1, maximum: 30, values: [] },
          { path: "video0.size", type: "string", read: true, write: true, disruptive: true, reason: null, minimum: null, maximum: null, values: [] },
        ] } : {}), quality: { presets: [{ id: "medium", available: true }, { id: "hd", available: true }] },
        day_night: { write: true, modes: ["auto", "day", "night"] }, power: { available: true, actions: ["on", "off", "restart"] } },
    }] });
  await page.route("**/api/**", async route => {
    const request = route.request();
    const path = new URL(request.url()).pathname;
    const body = request.postData() ? request.postDataJSON() : null;
    if (request.method() === "POST") posts.push({ path, body });
    let data: any = {};
    if (path === "/api/auth/me") data = { username: "fixture", org_id: 1, role_id: options.viewer ? 4 : 3,
      role: options.viewer ? "viewer" : "user", site_mode: "classic", permissions: ["video:view"], timezone: "UTC" };
    if (path.includes("/internal/v1/devices")) data = { items: [4619, 4623].map(device_id => ({
      device_id, sn: `fixture-${device_id}`, connection: { last_checked_result: !options.offline },
      device_tags: device_id === 4619 ? [{ tag: "sys", value: "ESP32" }, { tag: "camera", value: "openipc" }] : [{ tag: "sys", value: "ESP32" }],
    })), pages: 1, total: 2, stats: {} };
    if (path === "/api/settings/terminals") data = { items: [{ id: 1, device_id: 4619, sn: "fixture-4619", note: "Вход", address: "Первый этаж", is_active: true }], total_count: 1 };
    if (path.endsWith("/inventory")) data = { displays: [], cameras: [{ camera_id: "cam:ip000001", available: true }] };
    if (path.endsWith("/control/status")) data = { agent: { online: !options.offline }, lease: { active: !!options.busy } };
    if (path.endsWith("/control/lease") && request.method() === "POST") data = { lease_id: "11111111-1111-4111-8111-111111111111", keepalive_sec: 10 };
    if (path.endsWith("/stream/start") && options.rejectStart) { await route.fulfill({ status: 422, json: { detail: { code: "camera_h265_unsupported", message: "H.265 недоступен для выбранного источника" } } }); return; }
    if (path === "/api/cameras/4619") { reads++; data = { device_id: 4619, terminal_online: !options.offline, lease_active: !!options.busy, snapshot: snapshot() }; }
    if (path.endsWith("/commands") && request.method() === "POST") {
      if (options.unauthorized) { await route.fulfill({ status: 401, json: { detail: "Сессия истекла" } }); return; }
      if (options.lost) { await route.abort("failed"); return; }
      operation = "22222222-2222-4222-8222-222222222222";
      if (body.action === "quality") config = { ...config, video0: { size: "640x360", fps: 10, bitrate: 768 } };
      if (body.action === "power" && body.value === "off") power = false;
      if (body.action === "patch") for (const [section, fields] of Object.entries(body.patch)) config[section] = { ...config[section], ...fields as object };
      data = { id: operation };
    }
    if (path.includes("/commands/")) data = { id: operation, status: 3, results: [{ status_code: 200,
      result: { status: "ok", snapshot_id: "fixture-snapshot", applied_fields: [] } }] };
    await route.fulfill({ json: data });
  });
  await page.goto("/cameras?device_id=4619");
  await expect(page.getByText("Состояние и настройки камеры", { exact: true })).toBeVisible();
  return posts;
}

test("native field draft survives polling and sends only the changed field", async ({ page }) => {
  const posts = await cameraPortal(page, { native: true });
  await page.getByText("Видео и изображение", { exact: true }).click();
  await page.getByRole("spinbutton", { name: "Частота кадров, FPS" }).fill("20");
  await page.waitForTimeout(5500);
  await expect(page.getByRole("spinbutton", { name: "Частота кадров, FPS" })).toHaveValue("20");
  await page.getByRole("button", { name: "Применить изменения", exact: true }).click();
  await expect(page.getByText("Настройки применены и подтверждены камерой", { exact: true })).toBeVisible();
  expect(posts).toHaveLength(1);
  expect(posts[0].body).toMatchObject({ action: "patch", expected_revision: "a".repeat(64), patch: { video0: { fps: 20 } } });
  expect(Object.keys(posts[0].body.patch.video0)).toEqual(["fps"]);
});

test("new camera revision preserves the draft and blocks stale apply", async ({ page }) => {
  const posts = await cameraPortal(page, { native: true, revisionAfterPoll: true });
  await page.getByText("Видео и изображение", { exact: true }).click();
  await page.getByRole("spinbutton", { name: "Частота кадров, FPS" }).fill("20");
  await expect(page.getByText(/Ваши правки сохранены/)).toBeVisible({ timeout: 10000 });
  await expect(page.getByRole("spinbutton", { name: "Частота кадров, FPS" })).toHaveValue("20");
  await expect(page.getByRole("button", { name: "Применить изменения", exact: true })).toBeDisabled();
  expect(posts).toEqual([]);
});

test("discovery is explicit and an unverified port candidate cannot be bound", async ({ page }) => {
  const posts = await cameraPortal(page, { native: true });
  await page.getByText("Подключение камеры", { exact: true }).click();
  await expect(page.getByText("192.0.2.10", { exact: true })).toBeVisible();
  await expect(page.getByRole("listitem").filter({ hasText: "192.0.2.11" }).getByRole("button", { name: "Выбрать", exact: true })).toBeDisabled();
  expect(posts).toEqual([]);
  await page.getByRole("button", { name: "Найти камеру на port-2", exact: true }).click();
  await expect(page.getByText("Поиск завершён. Выберите камеру с подтверждённым port-2.", { exact: true })).toBeVisible();
  expect(posts).toHaveLength(1);
  expect(posts[0].body.action).toBe("discover");
});

test("explicit camera HEVC refusal never creates a legacy route or H264 restart", async ({ page }) => {
  await page.addInitScript(() => Object.defineProperty(RTCRtpReceiver, "getCapabilities", { value: () => ({ codecs: [{ mimeType: "video/H264" }, { mimeType: "video/H265" }] }) }));
  const posts = await cameraPortal(page, { native: true, rejectStart: true });
  await page.getByRole("button", { name: "Настройки видео", exact: true }).click();
  await page.getByRole("combobox", { name: "Кодек общего потока", exact: true }).click();
  await page.getByText("H.265", { exact: true }).last().click();
  await page.getByRole("button", { name: "Настройки видео", exact: true }).click();
  await page.getByRole("button", { name: "Начать просмотр", exact: true }).click();
  await expect(page.getByText("H.265 недоступен для выбранного источника").first()).toBeVisible();
  const starts = posts.filter(item => item.path.endsWith("/stream/start"));
  expect(starts).toHaveLength(1);
  expect(starts[0].body).toMatchObject({ mode: "camera", profile: "default", source_id: "cam:ip000001", codec_policy: "h265", browser_codecs: { h264: true, h265: true } });
  expect(posts.some(item => item.path.endsWith("/sessions"))).toBe(false);
});

test("camera list uses explicit tags, includes offline, and never starts on selection", async ({ page }) => {
  const posts = await cameraPortal(page, { offline: true });
  await expect(page.getByText("Первый этаж").first()).toBeVisible();
  await expect(page.getByText("4623", { exact: true })).toHaveCount(0);
  expect(posts).toEqual([]);
  await expect(page.getByRole("button", { name: "Начать просмотр", exact: true })).toBeDisabled();
});

test("quality applies with revision and correlated result, without stream commands", async ({ page }) => {
  const posts = await cameraPortal(page);
  await expect(page.getByText("Нажмите «Начать просмотр» над плеером.", { exact: true })).toBeVisible();
  await page.getByRole("button", { name: "Medium", exact: true }).click();
  await expect(page.getByText("Настройки применены и подтверждены камерой", { exact: true })).toBeVisible();
  expect(posts).toHaveLength(1);
  expect(posts[0].body).toMatchObject({ action: "quality", value: "medium", expected_revision: "a".repeat(64), allow_disruptive: true });
});

test("power off needs confirmation, cancelled action emits nothing", async ({ page }) => {
  const posts = await cameraPortal(page);
  await page.getByRole("button", { name: "Выключить", exact: true }).click();
  await page.getByRole("button", { name: "Отмена", exact: true }).click();
  await expect(page.getByRole("dialog")).toBeHidden();
  expect(posts).toEqual([]);
  await page.getByRole("button", { name: "Выключить", exact: true }).click();
  await page.getByRole("dialog").getByRole("button", { name: "Выключить", exact: true }).click();
  await expect(page.getByText(/Состояние выхода питания подтверждено/)).toBeVisible();
  expect(posts[0].body).toMatchObject({ action: "power", value: "off", expected_power_on: true });
});

for (const options of [{ busy: true }, { viewer: true }, { stale: true }]) {
  test(`settings stay disabled for ${JSON.stringify(options)}`, async ({ page }) => {
    const posts = await cameraPortal(page, options);
    await expect(page.getByRole("button", { name: "Medium", exact: true })).toBeDisabled();
    expect(posts).toEqual([]);
  });
}

test("lost command response is not replayed and remains blocked after reload", async ({ page }) => {
  const posts = await cameraPortal(page, { lost: true });
  await page.getByRole("button", { name: "HD", exact: true }).click();
  await expect(page.getByRole("button", { name: "HD", exact: true })).toBeDisabled();
  await page.reload();
  await expect(page.getByRole("button", { name: "HD", exact: true })).toBeDisabled();
  expect(posts).toHaveLength(1);
});

test("creation wizard submits Camera OpenIPC as ESP32 plus camera profile", async ({ page }) => {
  let created: any;
  await page.route("**/api/**", async route => {
    const path = new URL(route.request().url()).pathname;
    let data: any = {};
    if (path === "/api/auth/me") data = { username: "fixture", org_id: 1, role_id: 3, site_mode: "classic", timezone: "UTC", permissions: ["*"] };
    if (path === "/api/settings/terminals") {
      if (route.request().method() === "POST") {
        created = route.request().postDataJSON();
        data = { terminal_id: 9, device_id: 1000009, sn: "fixture-new", sys: "esp32", camera: "openipc",
          operation_id: "fixture", correlation_id: "fixture", pin: null,
          readiness: { record: "ready", certificate: "pending", iot: "ready", online: "offline" } };
      } else data = { items: [], total_count: 0 };
    }
    if (path.includes("/internal/v1/devices")) data = { items: [], pages: 1, total: 0 };
    await route.fulfill({ json: data });
  });
  await page.goto("/settings/terminals");
  await page.getByRole("button", { name: /Подключить терминал \(Мастер\)/ }).click();
  await page.getByLabel("Тип терминала").click();
  await page.getByText("Camera OpenIPC", { exact: true }).click();
  await page.getByRole("button", { name: /Создать и получить PIN/ }).click();
  await expect.poll(() => created).toMatchObject({ sys: "esp32", camera: "openipc" });
});

test("camera command 401 never refreshes or replays POST", async ({ page }) => {
  const posts = await cameraPortal(page, { unauthorized: true });
  await page.getByRole("button", { name: "HD", exact: true }).click();
  await expect(page.getByText("Сессия истекла", { exact: true })).toBeVisible();
  expect(posts).toHaveLength(1);
});
