import { expect, test, type Page } from "@playwright/test";

async function portal(page: Page, enabled: boolean) {
  const free = {
    terminal_id: 1,
    name: "Бесплатный терминал",
    state: "free",
    is_free: true,
    allowed: true,
    paid_until: null,
    grace_until: null,
    reason: "Бесплатно, без ограничения времени",
    action: null,
  };
  const extra = {
    ...free,
    terminal_id: 2,
    name: "Дополнительный терминал",
    state: enabled ? "unpaid" : "payments_disabled",
    is_free: false,
    allowed: false,
    reason: enabled
      ? "Требуется подписка"
      : "Платные подключения пока недоступны",
    action: enabled ? "Подключить" : null,
  };
  const requests: Array<{ operation_id: string; items: unknown[] }> = [];
  let paid = false;
  await page.route("**/api/**", async (route) => {
    const path = new URL(route.request().url()).pathname;
    let data: unknown = {};
    if (path === "/api/auth/me")
      data = {
        username: "owner@example.test",
        role_id: 5,
        role: "l4desk_owner",
        org_id: 9911,
        is_superuser: false,
        site_mode: "l4desk",
        timezone: "UTC",
        permissions: ["*"],
      };
    if (path === "/api/subscriptions")
      data = {
        payments_enabled: enabled,
        month_price_kopecks: 10000,
        can_create: true,
        items: [
          free,
          paid
            ? {
                ...extra,
                state: "active",
                allowed: true,
                paid_until: "2027-02-01T00:00:00Z",
                action: "Продлить",
              }
            : extra,
        ],
      };
    if (path === "/api/subscriptions/quote")
      data = {
        available: enabled,
        quote_hash: "reviewed-cart",
        snapshot: {
          amount_kopecks: 10000,
          items: [{ terminal_id: 2, months: 1 }],
        },
        preview: [{ terminal_id: 2, paid_until: "2027-02-01T00:00:00Z" }],
      };
    if (path === "/api/usage")
      data = [
        {
          terminal_id: 1,
          date: "2026-10-03",
          video_seconds: 9000,
          console_seconds: 120,
        },
      ];
    if (
      path === "/api/subscriptions/orders" &&
      route.request().method() === "POST"
    ) {
      requests.push(route.request().postDataJSON());
      if (requests.length === 1) {
        await route.fulfill({
          status: 502,
          json: { detail: "Uncertain payment creation" },
        });
        return;
      }
      paid = true;
      data = {
        id: 7,
        status: "succeeded",
        amount_kopecks: 10000,
        confirmation_url: null,
        items: [{ terminal_id: 2, months: 1 }],
        created_at: "2026-10-03T00:00:00Z",
      };
    } else if (path === "/api/subscriptions/orders") data = [];
    await route.fulfill({ json: data });
  });
  await page.goto("/licenses");
  await expect(
    page.getByRole("heading", { name: "Подписки", exact: true }),
  ).toBeVisible();
  return requests;
}

test("Payments disabled: one free terminal and prepared extras, no purchase", async ({
  page,
}) => {
  const requests = await portal(page, false);
  await expect(
    page.getByText("Бесплатно, без ограничения времени", { exact: true }),
  ).toBeVisible();
  await expect(
    page.getByRole("button", { name: "Оплата пока недоступна" }).first(),
  ).toBeDisabled();
  expect(requests).toHaveLength(0);
  await page.getByRole("tab", { name: "Использование" }).click();
  await expect(
    page.getByText("Всего: 2 ч. 32 мин.", { exact: true }),
  ).toBeVisible();
  await expect(
    page.getByRole("img", { name: "Длительность использования по дням" }),
  ).toBeVisible();
});

test("Reviewed cart retry uses the same order key and grants only the selected terminal", async ({
  page,
}) => {
  const requests = await portal(page, true);
  await page.getByRole("button", { name: "Подключить", exact: true }).click();
  await expect(page.getByRole("dialog")).toContainText(
    "Дополнительный терминал",
  );
  const payButton = page.getByRole("dialog").getByRole("button", { name: /Оплатить/ });
  await expect(payButton).toBeEnabled();
  await payButton.click();
  await expect(page.getByText(/Заказ мог быть создан/)).toBeVisible();
  await expect(payButton).toBeEnabled();
  await payButton.click();
  await expect(page.getByRole("dialog")).not.toBeVisible();
  await expect(
    page.getByRole("button", { name: "Продлить", exact: true }),
  ).toBeVisible();
  expect(requests).toHaveLength(2);
  expect(requests[0].operation_id).toBe(requests[1].operation_id);
  expect(requests[0].items).toEqual([{ terminal_id: 2, months: 1 }]);
});
