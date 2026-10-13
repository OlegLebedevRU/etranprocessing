import { expect, test, type Page } from "@playwright/test";

async function portal(page: Page) {
  const calls: { path: string; url: URL; method: string; body: any }[] = [];
  let disabled = false;
  await page.route("**/api/**", async route => {
    const request = route.request(); const url = new URL(request.url()); const path = url.pathname;
    calls.push({ path, url, method: request.method(), body: request.postData() ? request.postDataJSON() : null });
    let data: unknown = {};
    if (path === "/api/auth/me") data = { username: "fixture", org_id: 1, role_id: 5, is_superuser: false,
      site_mode: "l4desk", l4desk_licenses_enabled: true, classic_licenses_enabled: false, permissions: ["*"], session_id: "fixture" };
    if (path === "/api/admin/tenants/available") data = [];
    if (path === "/api/settings/terminals") {
      const current = Number(url.searchParams.get("page") || 1); const id = current === 1 ? 773 : 774;
      data = { items: [{ id, device_id: id, sn: `fixture${id}`, note: "Название терминала", address: "Адрес терминала",
        is_active: true, user_disabled: disabled, presence: disabled ? "disabled" : "online", suite_version: "1.13.43",
        inventory_published_at: "2026-10-12T12:00:00Z", cert_serial: "A".repeat(40), cert_not_valid_after: "2027-10-01T00:00:00Z",
        subscription: { terminal_id: id, state: "free", is_free: true, allowed: !disabled, reason: "Бесплатно" } }],
        total_count: 40, page: current, page_size: 20, can_create: true };
    }
    if (path.endsWith("/activity")) { disabled = !request.postDataJSON().is_active; data = { id: 773, is_active: true, user_disabled: disabled }; }
    if (path === "/api/internal/v1/devices/") data = { items: [{ id: 773, device_id: 773, sn: "fixture773",
      device_tags: [{ tag: "sys", value: "windows" }], connection: { device_id: 773, last_checked_result: true, svc_connect: true, is_svc_available: true } }], pages: 1, total: 1 };
    if (path.endsWith("/readiness")) data = { state: "ready", available: true, compatible: true,
      mqtt_available: true, write_available: true, capabilities: ["fs.mqtt_navigation"], missing_capabilities: [], valid_until: new Date(Date.now()+45000).toISOString() };
    await route.fulfill({ json: data });
  });
  await page.goto("/terminals?profile=l4desk");
  await expect(page.getByRole("button", { name: "Файловый менеджер 773", exact: true })).toBeVisible();
  return calls;
}

test("list is aggregated and paginated; version is a terminal fact", async ({ page }) => {
  const calls = await portal(page);
  await expect(page.getByText("L4 Tools: 1.13.43", { exact: true })).toBeVisible();
  expect(calls.some(c => c.path.includes("/internal/v1/devices") || c.path === "/api/subscriptions")).toBe(false);
  const list = calls.find(c => c.path === "/api/settings/terminals")!;
  expect(list.url.searchParams.get("l4desk")).toBe("true");
  expect(list.url.searchParams.has("all")).toBe(false);
  await page.getByTitle("2", { exact: true }).click();
  await expect(page.getByRole("button", { name: "Файловый менеджер 774", exact: true })).toBeVisible();
  expect(calls.filter(c => c.path === "/api/settings/terminals").at(-1)!.url.searchParams.get("page")).toBe("2");
});

for (const width of [390, 820, 1440]) test(`terminal layout fits ${width}px without horizontal scrolling`, async ({ page }) => {
  await page.setViewportSize({ width, height: 900 }); await portal(page);
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth)).toBe(true);
});

test("summary shares one passport snapshot with tags", async ({ page }) => {
  const calls = await portal(page);
  await page.getByRole("button", { name: "Сводка 773", exact: true }).click();
  await expect(page.getByText("Сводка терминала 773", { exact: true })).toBeVisible();
  await page.getByRole("tab", { name: "Теги", exact: true }).click();
  await expect(page.getByRole("tabpanel", { name: "Теги", exact: true }).getByText("windows", { exact: true })).toBeVisible();
  expect(calls.filter(c => c.path === "/api/internal/v1/devices/")).toHaveLength(1);
  expect(calls.some(c => c.method === "POST")).toBe(false);
});

test("FM navigation selects one terminal without acquiring a lease", async ({ page }) => {
  const calls = await portal(page);
  await page.getByRole("button", { name: "Файловый менеджер 773", exact: true }).click();
  await expect(page).toHaveURL(/\/files\?device_id=773/);
  await expect(page.getByRole("button", { name: "Подключиться к терминалу 773", exact: true })).toBeEnabled();
  const deviceCalls = calls.filter(c => c.path === "/api/internal/v1/devices/");
  expect(deviceCalls).toHaveLength(1); expect(deviceCalls[0].url.searchParams.get("device_id")).toBe("773");
  expect(calls.some(c => c.method === "POST")).toBe(false);
});

test("disable writes only the L4Desk scope and disables future actions", async ({ page }) => {
  const calls = await portal(page);
  await page.getByRole("button", { name: "Другие действия 773", exact: true }).click();
  await page.getByText("Отключить терминал", { exact: true }).click();
  await page.getByRole("button", { name: "Отключить", exact: true }).click();
  await expect(page.getByRole("button", { name: "Файловый менеджер 773", exact: true })).toBeDisabled();
  expect(calls.filter(c => c.method === "PATCH").map(c => c.body)).toEqual([{ is_active: false, scope: "l4desk" }]);
  expect(calls.some(c => /stop|revoke|rabbitmq/.test(c.path))).toBe(false);
});


test("Devices keeps unenrolled and disabled rows without requiring inventory", async ({ page }) => {
  await portal(page);
  await page.route("**/api/settings/terminals?*", async route => route.fulfill({ json: {
    items: [773,4623,4624].map(id => ({ id, device_id:id, sn:`fixture${id}`, is_active:id!==4624,
      user_disabled:false, presence:id===4624?"disabled":"online", subscription:id===773?{state:"free",allowed:true}:null })),
    total_count:3,page:1,page_size:20,can_create:true } }));
  await page.getByText("Все", {exact:true}).click();
  await expect(page.getByRole("heading", {name:"Устройства",exact:true})).toBeVisible();
  await expect(page.getByRole("button", {name:"Сводка 4623",exact:true})).toBeVisible();
  await expect(page.getByRole("button", {name:"Сводка 4624",exact:true})).toBeVisible();
});

test("Terminal screen accepts only sys=windows and never starts a stream on selection", async ({ page }) => {
  const posts:string[]=[];
  await page.route("**/api/**", async route => {
    const request=route.request(); const path=new URL(request.url()).pathname;
    if(request.method()==="POST") posts.push(path);
    let data:unknown={};
    if(path==="/api/auth/me") data={ username:"fixture",org_id:1,role_id:5,is_superuser:false,site_mode:"l4desk",permissions:["*"],session_id:"fixture" };
    if(path==="/api/admin/tenants/available") data=[];
    if(path==="/api/settings/terminals") data={items:[773,4623,4624,4625].map(id=>({id,device_id:id,sn:`fixture${id}`,is_active:true})),total_count:4};
    if(path==="/api/internal/v1/devices/") data={items:[773,4623,4624,4625].map((id,index)=>({id,device_id:id,sn:`fixture${id}`,
      device_tags:index===3?[]:[{tag:"sys",value:["windows","ESP32","linux"][index]}],
      connection:{last_checked_result:true,is_svc_available:true,svc_connect:true}})),total:4,pages:1};
    if(path.endsWith("/inventory")) data={displays:[],cameras:[]};
    if(path.endsWith("/stream-state")) data={streams:[]};
    if(path.endsWith("/control/status")) data={agent:{online:true},lease:{active:false}};
    await route.fulfill({json:data});
  });
  await page.setViewportSize({width:1440,height:900});
  await page.goto("/video?device_id=4623");
  await expect(page.getByRole("button", {name:/Терминал №773,/})).toBeVisible();
  for(const id of [4623,4624,4625]) await expect(page.getByRole("button", {name:new RegExp(`Терминал №${id},`)})).toHaveCount(0);
  await page.getByRole("button", {name:/Терминал №773,/}).click();
  await expect(page).toHaveURL(/device_id=773/);
  expect(posts).toEqual([]);
});
