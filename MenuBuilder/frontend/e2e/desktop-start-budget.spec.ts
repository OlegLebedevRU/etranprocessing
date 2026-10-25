import { expect, test, type Page } from "@playwright/test";

// Run the real page lifecycle against deferred HTTP and a signaling-only Janus.
// No producer, external WebSocket, or device is contacted.
async function portal(page: Page) {
  const posts: { path: string; body: any }[] = [];
  let completeLease: (() => void) | undefined;
  let completeStart: (() => void) | undefined;
  let signaling = 0;
  await page.clock.install();
  await page.routeWebSocket("**/fixture-janus", socket => {
    socket.onMessage(raw => {
      const message = JSON.parse(String(raw));
      if (message.janus === "message") signaling++;
      socket.send(JSON.stringify({ janus: "success", transaction: message.transaction,
        data: { id: message.janus === "create" ? 1 : 2 } }));
    });
  });
  await page.route("**/api/**", async route => {
    const request = route.request();
    const path = new URL(request.url()).pathname;
    if (request.method() === "POST") posts.push({ path, body: request.postData() ? request.postDataJSON() : null });
    let data: any = {};
    if (path === "/api/auth/me") data = { username: "fixture", org_id: 1, role_id: 3,
      role: "user", site_mode: "classic", permissions: ["video:view"], timezone: "UTC" };
    if (path.includes("/internal/v1/devices")) data = { items: [{ device_id: 773, sn: "fixture-773",
      connection: { last_checked_result: true }, device_tags: [{ tag: "sys", value: "windows" }] }], pages: 1, total: 1, stats: {} };
    if (path === "/api/settings/terminals") data = { items: [{ id: 1, device_id: 773, sn: "fixture-773", is_active: true }], total_count: 1 };
    if (path.endsWith("/inventory")) data = { displays: [{ id: "display:fixture", name: "Fixture display", width: 1920, height: 1080, is_primary: true, policy: "input" }], cameras: [] };
    if (path.endsWith("/control/status")) data = { agent: { online: true }, lease: { active: false } };
    if (path.endsWith("/control/lease") && request.method() === "POST") {
      await new Promise<void>(resolve => { completeLease = resolve; });
      data = { lease_id: "11111111-1111-4111-8111-111111111111", keepalive_sec: 10 };
    }
    if (path.endsWith("/stream/start")) {
      await new Promise<void>(resolve => { completeStart = resolve; });
      data = { stream_instance_id: "fixture-stream", state: "running", media: { janus_ws: "/fixture-janus", mountpoint_id: 1 } };
    }
    await route.fulfill({ json: data });
  });
  await page.goto("/video?device_id=773");
  await expect(page.getByRole("button", { name: "Запустить трансляцию", exact: true })).toBeEnabled();
  return { posts, lease: () => completeLease?.(), start: () => completeStart?.(), signaling: () => signaling };
}

for (const [leaseDelay, startDelay] of [[0, 35000], [9000, 115000]]) {
  test(`desktop START succeeds after lease ${leaseDelay}ms and HTTP ${startDelay}ms`, async ({ page }) => {
    const state = await portal(page);
    await page.getByRole("button", { name: "Запустить трансляцию", exact: true }).click();
    await expect.poll(() => state.posts.filter(p => p.path.endsWith("/control/lease")).length).toBe(1);
    await page.clock.fastForward(leaseDelay);
    state.lease();
    await expect.poll(() => state.posts.filter(p => p.path.endsWith("/stream/start")).length).toBe(1);
    await page.clock.fastForward(startDelay);
    state.start();
    await expect.poll(state.signaling).toBe(1);
    expect(state.posts.filter(p => p.path.endsWith("/stream/stop") || p.path.endsWith("/control/release"))).toEqual([]);
    await expect(page.getByText("Время запуска истекло.", { exact: false })).toHaveCount(0);
  });
}

test("cancelled desktop START cannot attach its late response", async ({ page }) => {
  const state = await portal(page);
  await page.getByRole("button", { name: "Запустить трансляцию", exact: true }).click();
  await expect.poll(() => state.posts.length).toBeGreaterThan(0);
  state.lease();
  await expect.poll(() => state.posts.filter(p => p.path.endsWith("/stream/start")).length).toBe(1);
  await page.getByText("Отменить запуск", { exact: true }).click();
  await page.clock.fastForward(35000);
  state.start();
  await expect.poll(() => state.posts.filter(p => p.path.endsWith("/stream/stop")).length).toBeGreaterThan(0);
  expect(state.signaling()).toBe(0);
  expect(state.posts.filter(p => p.path.endsWith("/stream/start"))).toHaveLength(1);
});
