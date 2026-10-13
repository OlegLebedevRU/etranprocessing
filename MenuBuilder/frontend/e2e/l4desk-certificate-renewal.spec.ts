import { expect, test, type Page } from "@playwright/test";

async function l4deskPortal(page: Page, superuser: boolean, active = true, denial?: string, recovery?: { recoverable: boolean }) {
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
      } else data = recovery ? { allowed: true, status: "installation_failed", installation_pending: true,
        recovery_pin_id: 327, recoverable: recovery.recoverable, requires_intervention: true, exit_code: 5 }
        : { allowed: active && !denial, reason: denial, status: queued ? "pending" : "none" };
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
    await page.getByRole("button", { name: "Другие действия 773", exact: true }).click();
    await page.getByText("Сертификат и PIN", { exact: true }).click();
    await page.getByRole("button", { name: "Выбрать удалённое продление", exact: true }).click();
    await page.getByRole("button", { name: "Продлить сертификат на терминале", exact: true }).click();
    await page.getByRole("button", { name: "Заказать продление", exact: true }).click();
    await expect(page.getByText("22222222-2222-4222-8222-222222222222", { exact: true })).toBeVisible();
    await expect(page.getByText("Ожидает терминал", { exact: true })).toBeVisible();
    expect(calls.filter(c => c.method === "POST")).toEqual([{ path: "/api/devices/773/certificate-renewal", method: "POST", body: { resume_installation: false } }]);
    expect(calls.some(c => c.path.includes("/billing/") || (c.path.endsWith("/pin") && c.method === "POST"))).toBe(false);
  });
}

test("L4Desk renewal uses the server denial and issues no command", async ({ page }) => {
  const calls = await l4deskPortal(page, true, true, "Сертификат истёк: используйте обычную выдачу PIN");
  await page.getByRole("button", { name: "Другие действия 773", exact: true }).click();
    await page.getByText("Сертификат и PIN", { exact: true }).click();
    await page.getByRole("button", { name: "Выбрать удалённое продление", exact: true }).click();
  await expect(page.getByRole("button", { name: "Продлить сертификат на терминале", exact: true })).toBeDisabled();
  await expect(page.getByText("Сертификат истёк: используйте обычную выдачу PIN", { exact: true })).toBeVisible();
  expect(calls.some(c => c.method === "POST")).toBe(false);
});

test("L4Desk administratively disabled terminal cannot open renewal", async ({ page }) => {
  const calls = await l4deskPortal(page, true, false);
  await page.getByRole("button", { name: "Другие действия 773", exact: true }).click();
  await page.getByText("Сертификат и PIN", { exact: true }).click();
    await page.getByRole("button", { name: "Выбрать удалённое продление", exact: true }).click();
  await expect(page.getByRole("button", { name: "Продлить сертификат на терминале", exact: true })).toBeDisabled();
  expect(calls.some(c => c.method === "POST")).toBe(false);
});

test("Failed installation blocks a new order and resumes the original issuance without payment", async ({ page }) => {
  const calls = await l4deskPortal(page, true, true, undefined, { recoverable: true });
  await page.getByRole("button", { name: "Другие действия 773", exact: true }).click();
    await page.getByText("Сертификат и PIN", { exact: true }).click();
    await page.getByRole("button", { name: "Выбрать удалённое продление", exact: true }).click();
  await expect(page.getByRole("button", { name: "Продлить сертификат на терминале", exact: true })).toBeDisabled();
  await expect(page.getByText("Сертификат выпущен, но установка завершилась ошибкой", { exact: true })).toBeVisible();
  await page.getByRole("button", { name: "Повторить установку выпущенного сертификата", exact: true }).click();
  await page.getByRole("button", { name: "Повторить установку", exact: true }).click();
  await expect(page.getByText("22222222-2222-4222-8222-222222222222", { exact: true })).toBeVisible();
  expect(calls.filter(c => c.method === "POST")).toEqual([{ path: "/api/devices/773/certificate-renewal",
    method: "POST", body: { pin_id: 327, resume_installation: true } }]);
  expect(calls.some(c => c.path.includes("/billing/") || c.path.endsWith("/pin"))).toBe(false);
});

test("Expired recovery escalates without offering a new issuance or sending a command", async ({ page }) => {
  const calls = await l4deskPortal(page, true, true, undefined, { recoverable: false });
  await page.getByRole("button", { name: "Другие действия 773", exact: true }).click();
    await page.getByText("Сертификат и PIN", { exact: true }).click();
    await page.getByRole("button", { name: "Выбрать удалённое продление", exact: true }).click();
  await expect(page.getByRole("button", { name: "Продлить сертификат на терминале", exact: true })).toBeDisabled();
  await expect(page.getByRole("button", { name: "Повторить установку выпущенного сертификата", exact: true })).toBeDisabled();
  await expect(page.getByText(/Повторная установка сейчас недоступна/)).toBeVisible();
  expect(calls.some(c => c.method === "POST")).toBe(false);
});


test("shared renewal keeps modal confirmation in Classic settings", async ({page})=>{
  const calls=await l4deskPortal(page,true);
  await page.goto("/settings/terminals?profile=classic");
  await page.getByRole("button").filter({has:page.locator(".anticon-safety-certificate")}).click();
  await expect(page.getByRole("dialog",{name:"Сертификат терминала 773",exact:true})).toBeVisible();
  await page.getByRole("button",{name:"Продлить сертификат на терминале",exact:true}).click();
  await expect(page.getByRole("dialog",{name:"Удалённое продление сертификата",exact:true})).toBeVisible();
  await page.getByRole("button",{name:"Заказать продление",exact:true}).click();
  await expect(page.getByText("22222222-2222-4222-8222-222222222222",{exact:true})).toBeVisible();
  expect(calls.filter(c=>c.method==="POST"&&c.path.endsWith("/certificate-renewal"))).toHaveLength(1);
});
