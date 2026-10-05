import { expect, test, type Page } from "@playwright/test";

async function l4deskPortal(page: Page, superuser: boolean, active = true, denial?: string) {
  const calls: Array<{ path: string; method: string; body: unknown }> = [];
  let queued = false;
  await page.route("**/api/**", async route => {
    const request = route.request();
    const path = new URL(request.url()).pathname;
    calls.push({ path, method: request.method(), body: request.postData() ? request.postDataJSON() : null });
    let data: unknown = {};
    if (path === "/api/auth/me") data = { username: "fixture-l4desk", org_id: 1,
      role_id: superuser ? 1 : 5, is_superuser: superuser, site_mode: superuser ? "both" : "l4desk",
      classic_licenses_enabled: false, l4desk_licenses_enabled: true, timezone: "UTC", permissions: ["*"] };
    if (path === "/api/admin/tenants/available") data = [{ org_id: 1, org_name: "Fixture", is_active: true }];
    if (path === "/api/settings/terminals") data = { items: [{ id: 1, device_id: 773, sn: "fixture-sn",
      is_active: active, cert_serial: "A".repeat(40), cert_not_valid_after: "2027-10-01T00:00:00Z",
      readiness: { certificate: "consumed", iot: "ready", online: "offline" }, created_at: "2026-10-01T00:00:00Z" }],
      total_count: 1, page: 1, page_size: 20 };
    if (path === "/api/subscriptions") data = { payments_enabled: false, items: [{ terminal_id: 1, device_id: 773,
      state: "free", allowed: true, is_free: true }], can_create: true };
    if (path.includes("/internal/v1/devices")) data = { items: [], pages: 1, total: 0 };
    if (path === "/api/devices/773/certificate-renewal") {
      if (request.method() === "POST") {
        queued = true;
        data = { task_id: "22222222-2222-4222-8222-222222222222", pin_id: 9,
          expires_at: "2026-10-12T00:00:00Z", status: "queued" };
      } else data = { allowed: !denial, reason: denial, status: queued ? "pending" : "none" };
    }
    await route.fulfill({ json: data });
  });
  await page.goto("/terminals?profile=l4desk");
  await page.getByText("Все", { exact: true }).click();
  await expect(page.getByRole("row").filter({ hasText: "773" })).toBeVisible();
  return calls;
}

for (const superuser of [true, false]) {
  test(`L4Desk ${superuser ? "superuser" : "owner"} queues authenticated renewal for an offline terminal`, async ({ page }) => {
    const calls = await l4deskPortal(page, superuser);
    expect(calls.some(c => c.path.endsWith("/certificate-renewal"))).toBe(false);
    await page.getByRole("button", { name: "Продлить сертификат 773", exact: true }).click();
    await page.getByRole("button", { name: "Продлить сертификат на терминале", exact: true }).click();
    await page.getByRole("button", { name: "Заказать продление", exact: true }).click();
    await expect(page.getByText("22222222-2222-4222-8222-222222222222", { exact: true })).toBeVisible();
    await expect(page.getByText("Ожидает терминал", { exact: true })).toBeVisible();
    expect(calls.filter(c => c.method === "POST")).toEqual([{ path: "/api/devices/773/certificate-renewal", method: "POST", body: {} }]);
    expect(calls.some(c => c.path.includes("/billing/") || c.path.endsWith("/pin"))).toBe(false);
  });
}

test("L4Desk renewal uses the server denial and issues no command", async ({ page }) => {
  const calls = await l4deskPortal(page, true, true, "Сертификат истёк: используйте обычную выдачу PIN");
  await page.getByRole("button", { name: "Продлить сертификат 773", exact: true }).click();
  await expect(page.getByRole("button", { name: "Продлить сертификат на терминале", exact: true })).toBeDisabled();
  await expect(page.getByText("Сертификат истёк: используйте обычную выдачу PIN", { exact: true })).toBeVisible();
  expect(calls.some(c => c.method === "POST")).toBe(false);
});

test("L4Desk administratively disabled terminal cannot open renewal", async ({ page }) => {
  const calls = await l4deskPortal(page, true, false);
  await expect(page.getByRole("button", { name: "Продлить сертификат 773", exact: true })).toBeDisabled();
  expect(calls.some(c => c.path.endsWith("/certificate-renewal"))).toBe(false);
});
