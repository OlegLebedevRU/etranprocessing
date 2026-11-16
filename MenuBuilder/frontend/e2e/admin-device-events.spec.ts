import { expect, test, type Page } from "@playwright/test";

async function fixture(page: Page, superuser = true) {
  const calls: Array<{ path: string; method: string; body: any }> = [];
  let state = "new";
  let change: any = null;
  await page.route("**/api/**", async route => {
    const req = route.request(); const url = new URL(req.url()); const path = url.pathname;
    const body = req.postData() ? req.postDataJSON() : null;
    calls.push({ path, method: req.method(), body });
    let data: any = {};
    if (path === "/api/auth/me") data = { username: "fixture-admin", org_id: 1, role_id: superuser ? 1 : 2, role: superuser ? "superuser" : "user", is_superuser: superuser, site_mode: "classic", classic_licenses_enabled: true, permissions: ["*"] };
    if (path === "/api/admin/tenants/available" || path === "/api/admin/organizations") data = [{ org_id: 1, org_name: "Tenant A", site_mode: "classic", is_active: true }, { org_id: 2, org_name: "Tenant B", site_mode: "classic", is_active: true }];
    if (path === "/api/admin/terminal-types") data = [];
    if (path === "/api/admin/terminals") data = { items: [{ id: 1, device_id: 773, sn: "fixture-sn", org_id: 1, org_name: "Tenant A", is_active: true, terminal_type_id: 0, iot_provisioned: true, cert_serial: "A".repeat(40) }], total: 1, page: 1, page_size: 50 };
    if (path === "/api/admin/device-events/count") data = { new_count: state === "new" ? 1 : 0 };
    if (path === "/api/admin/device-events/1" && req.method() === "PATCH") { state = body.status; data = { id: 1, status: state }; }
    if (path === "/api/admin/device-events") data = { total: state === "new" ? 1 : 0, items: state === "new" ? [{ id: 1, terminal_id: 1, device_id: 773, sn: "fixture-sn", org_id: 2, operation_id: "11111111-1111-4111-8111-111111111111", event_type: "identity_old_certificate", status: state, details: { source_org_id: 1, target_org_id: 2 }, created_at: "2026-11-16T12:00:00Z" }] : [] };
    if (path === "/api/admin/terminals/1/identity-change") {
      if (req.method() === "POST") change = { ...body, source_org_id: 1, status: "pending", requested_by: "fixture-admin", created_at: "2026-11-16T12:00:00Z", issued_at: null };
      data = change;
    }
    if (path === "/api/admin/terminals/1/generate-pin") data = { pin: "fixture-pin", terminal_id: 1, device_id: 773, expires_at: "2027-01-01T00:00:00Z" };
    await route.fulfill({ json: data });
  });
  return calls;
}

test("superuser badges count new incidents and reading does not acknowledge them", async ({ page }) => {
  const calls = await fixture(page);
  await page.goto("/admin/device-events");
  const admin = page.getByRole("menuitem", { name: /Администрирование/ }).first();
  await expect(admin.locator(".ant-badge-count")).toHaveAttribute("title", "1");
  await expect(page.getByRole("row").filter({ hasText: "fixture-sn" })).toBeVisible();
  expect(calls.some(call => call.method === "PATCH")).toBe(false);
  await page.getByRole("button", { name: "Обработать", exact: true }).click();
  await page.getByRole("combobox", { name: "Новое состояние" }).click();
  await page.getByText("Закрыто", { exact: true }).last().click();
  await page.getByRole("textbox", { name: "Комментарий администратора" }).fill("Новый сертификат установлен");
  await page.getByRole("button", { name: "Сохранить", exact: true }).click();
  await expect(admin.locator(".ant-badge-count")).toHaveCount(0);
  await expect(page.getByRole("row").filter({ hasText: "fixture-sn" })).toHaveCount(0);
  expect(calls.find(call => call.method === "PATCH")?.body).toEqual({ status: "closed", comment: "Новый сертификат установлен" });
});

test("ordinary user cannot open the inbox and does not poll its counter", async ({ page }) => {
  const calls = await fixture(page, false);
  await page.goto("/admin/device-events");
  await expect(page.getByText("403", { exact: true })).toBeVisible();
  expect(calls.some(call => call.path.includes("device-events"))).toBe(false);
});

test("identity action prepares the target and preserves the source until setup", async ({ page }) => {
  const calls = await fixture(page);
  await page.goto("/admin/terminals");
  await page.getByRole("button", { name: /^Действия/ }).click();
  await page.getByRole("menuitem", { name: /Сменить идентичность/ }).click();
  await page.getByRole("combobox", { name: "Новый тенант" }).click();
  await page.getByText("Tenant B (#2)", { exact: true }).last().click();
  await page.getByRole("button", { name: "Подготовить смену идентичности", exact: true }).click();
  await expect(page.getByText("Ожидается получение сертификата", { exact: true })).toBeVisible();
  const request = calls.find(call => call.method === "POST" && call.path.endsWith("/identity-change"));
  expect(request?.body.target_org_id).toBe(2);
  expect(request?.body.operation_id).toMatch(/^[0-9a-f-]{36}$/);
  expect(calls.some(call => call.path.includes("/setup") || call.path.includes("identity-transfers"))).toBe(false);
  await expect(page.locator(".ant-table-row").filter({ hasText: "fixture-sn" })).toContainText("Tenant A");
});
