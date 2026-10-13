import { expect, test, type Page } from "@playwright/test";

async function portal(page: Page, options: { superuser?: boolean; tenant?: number; adminDisabled?: boolean; userDisabled?: boolean; blocked?: boolean } = {}) {
  const calls: { path: string; method: string; body: any; url: URL }[] = [];
  let issued = false;
  let note = "До обновления";
  let hold: (() => void) | undefined;
  let pauseNext = false;
  const tenant = options.tenant ?? 1;
  await page.route("**/api/**", async route => {
    const request = route.request(); const url = new URL(request.url()); const path = url.pathname;
    calls.push({ path, method: request.method(), body: request.postData() ? request.postDataJSON() : null, url });
    let data: unknown = {};
    if (path === "/api/auth/me") data = { username: "fixture", org_id: tenant, role_id: options.superuser ? 1 : 5,
      is_superuser: !!options.superuser, site_mode: options.superuser ? "both" : "l4desk", permissions: ["*"], session_id: "fixture" };
    if (path === "/api/admin/tenants/available") data = [{ org_id: tenant, org_name: "Fixture", is_active: true }];
    if (path === "/api/settings/terminals") {
      if (pauseNext) { pauseNext = false; await new Promise<void>(resolve => { hold = resolve; }); }
      data = { items: [{ id: 7, device_id: 773, sn: "fixture-sn", note, address: "Исходный адрес",
        is_active: !options.adminDisabled, user_disabled: !!options.userDisabled, iot_blocked: !!options.blocked,
        presence: options.adminDisabled || options.userDisabled ? "disabled" : "online",
        cert_serial: "A".repeat(40), cert_not_valid_after: "2027-10-01T00:00:00Z",
        subscription: { state: "free", allowed: !options.adminDisabled && !options.userDisabled, is_free: true } }],
        total_count: 1, page: 1, page_size: 20, can_create: true, presence_unavailable: false };
    }
    if (path === "/api/settings/terminals/7") data = { id: 7, device_id: 773, ...request.postDataJSON() };
    if (path === "/api/settings/terminals/7/pin") {
      if (request.method() === "POST") issued = true;
      data = issued ? { pin: "012345", status: "issued", operation_id: "fixture", expires_at: "2027-10-01T00:00:00Z" } : null;
    }
    if (path === "/api/devices/773/certificate-renewal") data = { allowed: true, status: "none" };
    await route.fulfill({ json: data });
  });
  await page.setViewportSize({ width: 1440, height: 900 });
  await page.goto("/terminals?profile=l4desk");
  await expect(page.getByRole("button", { name: "Другие действия 773", exact: true })).toBeVisible();
  return { calls, pausePolling: () => { pauseNext = true; note = "После обновления"; }, resume: () => hold?.() };
}

for (const tenant of [1, 1000]) test(`superuser edits and issues manual PIN in active tenant ${tenant}`, async ({ page }) => {
  const { calls } = await portal(page, { superuser: true, tenant });
  await page.getByRole("button", { name: "Другие действия 773", exact: true }).click();
  await page.getByText("Редактировать", { exact: true }).click();
  await page.getByLabel("Адрес установки").fill("Обновлённый адрес");
  await page.getByRole("button", { name: "Сохранить", exact: true }).click();
  await expect(page.getByRole("dialog")).toHaveCount(0);
  expect(calls.find(c => c.path === "/api/settings/terminals")!.url.searchParams.get("org_id")).toBe(String(tenant));
  expect(calls.find(c => c.method === "PATCH")!.body.address).toBe("Обновлённый адрес");
  await page.getByRole("button", { name: "Другие действия 773", exact: true }).click();
  await page.getByText("Сертификат и PIN", { exact: true }).click();
  await expect(page.getByRole("button", { name: "Выбрать удалённое продление", exact: true })).toBeVisible();
  await expect(page.getByRole("button", { name: "Выбрать ручную установку", exact: true })).toBeVisible();
  expect(calls.some(c => c.path.endsWith("/pin") || c.path.endsWith("/certificate-renewal"))).toBe(false);
  await page.getByRole("button", { name: "Выбрать ручную установку", exact: true }).click();
  await page.getByRole("button", { name: "Выдать PIN для ручной установки", exact: true }).click();
  await expect(page.getByRole("button", { name: "Выбрать другой способ", exact: true })).toBeDisabled();
  await page.getByRole("button", { name: "Выдать PIN", exact: true }).click();
  await expect(page.getByText("012345", { exact: true })).toBeVisible();
  const pinPosts = calls.filter(c => c.method === "POST" && c.path.endsWith("/pin"));
  expect(pinPosts).toHaveLength(1);
  expect(pinPosts[0].body.operation_id).toMatch(/^[0-9a-f-]{36}$/);
  expect(calls.some(c => c.path.endsWith("/certificate-renewal"))).toBe(false);
});

for (const userDisabled of [false, true]) test(`admin disable blocks user toggle with userDisabled=${userDisabled}`, async ({ page }) => {
  const { calls } = await portal(page, { adminDisabled: true, userDisabled });
  await expect(page.getByText("Отключено администратором", { exact: true })).toBeVisible();
  if (userDisabled) await expect(page.getByText("Отключено пользователем", { exact: true })).toBeVisible();
  await page.getByRole("button", { name: "Другие действия 773", exact: true }).click();
  await expect(page.getByRole("menuitem", { name: userDisabled ? /Включить терминал/ : /Отключить терминал/ })).toHaveAttribute("aria-disabled", "true");
  expect(calls.some(c => c.method === "PATCH" || c.method === "POST")).toBe(false);
});

test("silent polling preserves rows and an unfinished edit", async ({ page }) => {
  await page.clock.install();
  const state = await portal(page);
  const summary = page.getByRole("button", { name: "Сводка 773", exact: true });
  await summary.evaluate(element => { element.setAttribute("data-original-row", "yes"); });
  await page.getByRole("button", { name: "Другие действия 773", exact: true }).click();
  await page.getByText("Редактировать", { exact: true }).click();
  const address = page.getByLabel("Адрес установки");
  await address.fill("Несохранённое изменение");
  state.pausePolling();
  await page.clock.fastForward(33000);
  await expect.poll(() => state.calls.filter(c => c.path === "/api/settings/terminals").length).toBe(2);
  await expect(page.locator(".ant-table-wrapper .ant-spin-spinning")).toHaveCount(0);
  await expect(summary).toHaveAttribute("data-original-row", "yes");
  state.resume();
  await expect(page.getByText("После обновления", { exact: true })).toBeVisible();
  await expect(address).toHaveValue("Несохранённое изменение");
  await expect(summary).toHaveAttribute("data-original-row", "yes");
});

test("IoT block has its own status label", async ({ page }) => {
  await portal(page, { blocked: true });
  await expect(page.getByText("Заблокировано IoT", { exact: true })).toBeVisible();
  await expect(page.getByRole("button", { name: "Консоль 773", exact: true })).toBeDisabled();
});


test("certificate chooser fits a narrow screen and requires explicit flow selection", async ({ page }) => {
  const { calls } = await portal(page);
  await page.setViewportSize({ width:390, height:844 });
  await page.getByRole("button",{name:"Другие действия 773",exact:true}).click();
  await page.getByText("Сертификат и PIN",{exact:true}).click();
  await expect(page.getByRole("button",{name:"Выбрать удалённое продление",exact:true})).toBeVisible();
  await expect(page.getByRole("button",{name:"Выбрать ручную установку",exact:true})).toBeVisible();
  expect(await page.evaluate(()=>document.documentElement.scrollWidth<=window.innerWidth)).toBe(true);
  expect(calls.some(c=>c.path.endsWith("/pin")||c.path.endsWith("/certificate-renewal"))).toBe(false);
  await expect.poll(async () => {
    const box=await page.locator(".ant-drawer-content-wrapper").boundingBox();
    return !!box && Math.abs(box.x)<1 && box.width<=391;
  }).toBe(true);
  await expect(page.getByRole("menuitem",{name:/Сертификат и PIN/})).toHaveCount(0);
  await page.screenshot({path:"test-results/certificate-chooser-390.png",fullPage:true});
});


test("manual PIN retry preserves operation after an ambiguous response", async ({page})=>{
  await portal(page,{superuser:true});
  const operations:string[]=[];
  await page.route("**/api/settings/terminals/7/pin",async route=>{
    if(route.request().method()==="GET") { await route.fulfill({json:null});return; }
    const operation=route.request().postDataJSON().operation_id;operations.push(operation);
    if(operations.length===1) await route.abort("failed");
    else await route.fulfill({json:{pin:"012345",status:"issued",operation_id:operation,expires_at:"2027-10-01T00:00:00Z"}});
  });
  await page.getByRole("button",{name:"Другие действия 773",exact:true}).click();
  await page.getByText("Сертификат и PIN",{exact:true}).click();
  await page.getByRole("button",{name:"Выбрать ручную установку",exact:true}).click();
  await page.getByRole("button",{name:"Выдать PIN для ручной установки",exact:true}).click();
  await page.getByRole("button",{name:"Выдать PIN",exact:true}).click();
  await expect(page.getByText("Результат выдачи PIN пока не подтверждён",{exact:true})).toBeVisible();
  await expect(page.getByRole("dialog")).toHaveCount(1);
  await page.getByRole("button",{name:"Повторить запрос PIN",exact:true}).click();
  await page.getByRole("dialog").last().getByRole("button",{name:"Повторить запрос PIN",exact:true}).click();
  await expect(page.getByText("012345",{exact:true})).toBeVisible();
  expect(operations).toHaveLength(2);expect(operations[0]).toBe(operations[1]);
});
