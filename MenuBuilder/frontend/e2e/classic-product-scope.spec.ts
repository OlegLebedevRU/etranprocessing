import { expect, test, type Page } from "@playwright/test";

async function classicPortal(page: Page) {
  let active = true;
  const calls: Array<{ path: string; method: string; body: any }> = [];
  await page.route("**/api/**", async route => {
    const request = route.request();
    const path = new URL(request.url()).pathname;
    calls.push({ path, method: request.method(), body: request.postData() ? request.postDataJSON() : null });
    let data: any = {};
    if (path === "/api/auth/me") data = { username: "fixture-classic", org_id: 1, role_id: 3,
      site_mode: "classic", is_superuser: false, classic_licenses_enabled: true,
      l4desk_licenses_enabled: false, timezone: "UTC", permissions: ["*"] };
    if (path === "/api/settings/terminals/1/activity") { active = request.postDataJSON().is_active; data = { id: 1, is_active: active }; }
    if (path === "/api/settings/terminals") data = { items: [{ id: 1, device_id: 773, sn: "fixture-sn",
      is_active: active, cert_serial: "A".repeat(40), readiness: { certificate: "consumed", iot: "ready" },
      created_at: "2026-10-01T00:00:00Z" }], total_count: 1, page: 1, page_size: 50 };
    if (path === "/api/devices/773/certificate-renewal") data = { allowed: true, status: "none" };
    await route.fulfill({ json: data });
  });
  await page.goto("/settings/terminals?profile=l4desk");
  await expect(page.getByRole("row").filter({ hasText: "773" })).toBeVisible();
  return calls;
}

test("Classic disable is reversible and subscription routes remain unavailable", async ({ page }) => {
  const calls = await classicPortal(page);
  const row = page.getByRole("row").filter({ hasText: "773" });
  await row.getByRole("button", { name: /Отключить$/ }).click();
  await expect(page.getByText(/Льгота бесплатного терминала/)).toHaveCount(0);
  await page.getByRole("button", { name: "Отключить", exact: true }).last().click();
  await expect(row.getByRole("button", { name: /Включить$/ })).toBeVisible();
  await row.getByRole("button", { name: /Включить$/ }).click();
  await page.getByRole("button", { name: "Включить", exact: true }).last().click();
  await expect(row.getByRole("button", { name: /Отключить$/ })).toBeVisible();
  expect(calls.filter(call => call.path.endsWith("/activity")).map(call => call.body.is_active)).toEqual([false, true]);
  expect(calls.some(call => call.method === "DELETE")).toBe(false);
  await page.goto("/licenses?profile=l4desk");
  await expect(page.getByRole("heading", { name: "Подписки", exact: true })).toHaveCount(0);
  expect(calls.some(call => call.path === "/api/subscriptions")).toBe(false);
});

test("Paid renewal waits for payment and reuses order and hidden PIN on a queue retry", async ({ page }) => {
  await classicPortal(page);
  const orderId = "11111111-1111-4111-8111-111111111111";
  const taskId = "22222222-2222-4222-8222-222222222222";
  const queued: any[] = [];
  let confirmed = false;
  await page.route("**/api/billing/terminals/1/certificate-pin?purpose=renew", route => route.fulfill({ json: {
    status: "payment_required", payment_required: true, terminal_id: 1, order_id: orderId,
    amount_minor: 100, currency: "RUB", payment_url: "https://checkout.example.test/payment",
  } }));
  await page.route(`**/api/billing/orders/${orderId}/confirm`, route => {
    confirmed = true;
    return route.fulfill({ json: { order_id: orderId, status: "paid", items_updated: 1 } });
  });
  await page.route("**/api/devices/773/certificate-renewal", route => {
    if (route.request().method() === "GET") return route.fulfill({ json: { allowed: true, status: "none" } });
    queued.push(route.request().postDataJSON());
    if (!confirmed) return route.fulfill({ status: 402, json: { detail: {
      code: "certificate_payment_required", terminal_id: 1, purpose: "renew",
    } } });
    if (queued.length === 2) return route.fulfill({ status: 503, json: { detail: {
      message: "Постановка не подтверждена", pin_id: 9, order_id: orderId,
    } } });
    return route.fulfill({ json: { pin_id: 9, task_id: taskId, status: "queued", expires_at: "2026-10-12T00:00:00Z" } });
  });
  await page.locator(".ant-table-row button:has(.anticon-safety-certificate)").click();
  await page.getByRole("button", { name: "Продлить сертификат на терминале", exact: true }).click();
  await page.getByRole("button", { name: "Заказать продление", exact: true }).click();
  await expect(page.getByRole("link", { name: "Перейти к оплате" })).toBeVisible();
  expect(confirmed).toBe(false);
  await expect(page.getByRole("button", { name: "Эмулировать оплату" })).toHaveCount(0);
  await page.getByRole("button", { name: "Проверить оплату и поставить в очередь" }).click();
  await page.getByRole("button", { name: /Повторить \(1\/3\)$/ }).click();
  await expect(page.getByText(taskId, { exact: true })).toBeVisible();
  expect(queued).toEqual([{}, { order_id: orderId }, { pin_id: 9, order_id: orderId }]);
  await expect(page.getByText("000001", { exact: true })).toHaveCount(0);
});
