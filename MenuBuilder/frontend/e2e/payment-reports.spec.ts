import { expect, test } from "@playwright/test";
import { readFile } from "node:fs/promises";

test("payment type filters reach API and CSV includes every parameter", async ({ page }) => {
  const requested: string[] = [];
  const base = {
    paym_id: 1, paym_ext_id: "0262_fixture_cash", device_id: 262, sn: "SN262",
    paym_tsp_code: 7, tsp_name: "Услуга", paym_datetime: "2026-11-10T12:00:00Z",
    paym_amount: 10000, paym_state: 2, paym_state_label: "Оплачен",
    pay_type_id: 1, pay_type_label: "Наличные",
    params: [{ code: 1, description: "ФИО", value: "Иванов И.И." }],
  };
  await page.route("**/api/**", async route => {
    const url = new URL(route.request().url());
    let data: unknown = {};
    if (url.pathname === "/api/auth/me") data = {
      username: "reports@example.test", role: "user", role_id: 2, org_id: 475,
      is_superuser: false, site_mode: "classic", timezone: "Europe/Moscow", permissions: ["*"],
    };
    if (url.pathname === "/api/services") data = { data: [] };
    if (url.pathname === "/api/reports/payments") {
      const type = url.searchParams.get("payment_type") || "all";
      requested.push(type);
      const card = { ...base, paym_id: 2, paym_ext_id: "0262_fixture_card", pay_type_id: 2, pay_type_label: "Карта",
        params: [{ code: 2, description: "Количество", value: "1 шт." }] };
      const items = type === "card" ? [card] : type === "cash" ? [base] : [base, card];
      data = { items, total: items.length };
    }
    await route.fulfill({ json: data });
  });
  await page.goto("/reports?tab=payments");
  await expect(page.getByText("0262_fixture_cash", { exact: true })).toBeVisible();
  expect(requested.at(-1)).toBe("all");
  const filter = page.getByRole("combobox", { name: "Тип платежа" });
  await filter.click();
  await filter.press("ArrowDown");
  await filter.press("Enter");
  await expect(page.getByText("0262_fixture_cash", { exact: true })).toHaveCount(0);
  expect(requested.at(-1)).toBe("card");
  await filter.click();
  await filter.press("ArrowDown");
  await filter.press("Enter");
  await expect(page.getByText("0262_fixture_cash", { exact: true })).toBeVisible();
  expect(requested.at(-1)).toBe("cash");
  await filter.click();
  await filter.press("ArrowUp");
  await filter.press("ArrowUp");
  await filter.press("Enter");
  await expect(page.getByText("0262_fixture_card", { exact: true })).toBeVisible();
  const downloaded = page.waitForEvent("download");
  await page.getByRole("button", { name: /Экспорт/ }).click();
  const file = await downloaded;
  const csv = await readFile((await file.path())!, "utf8");
  expect(csv).toContain("Параметр 1: ФИО;Параметр 2: Количество");
  expect(csv).toContain("Иванов И.И.");
  expect(csv).toContain("1 шт.");
  expect(csv).toContain("0262_fixture_cash");
  expect(csv).toContain("0262_fixture_card");
});
