import { test, expect, type Page } from "@playwright/test";

async function portal(page: Page, profile: "classic"|"l4desk", available=true, conflict=false) {
  const calls: Array<{path:string;method:string;body:any}> = [];
  const lease="22222222-2222-4222-8222-222222222222";
  await page.route("**/api/**", async route => {
    const request=route.request(),path=new URL(request.url()).pathname;
    calls.push({path,method:request.method(),body:request.postData()?request.postDataJSON():null});
    let data:any={};
    if(path==="/api/auth/me") data={username:"fixture",org_id:7,role_id:3,is_superuser:false,site_mode:"both",timezone:"UTC",permissions:["*"],session_id:"fixture-browser"};
    if(path==="/api/admin/tenants/available")data=[];
    if(path==="/api/settings/terminals")data={items:[{id:1,device_id:10,sn:"fixture",is_active:true}],total_count:1};
    if(path.includes("/internal/v1/devices"))data={items:[{id:1,device_id:10,sn:"fixture",device_tags:[],connection:{device_id:10,last_checked_result:true,svc_connect:true,is_svc_available:true}}],pages:1,total:1};
    if(path.endsWith("/readiness"))data={state:available?"ready":"incompatible",available,compatible:available,mqtt_available:true,server_time:new Date().toISOString(),valid_until:new Date(Date.now()+45000).toISOString(),agent_version:"1.0-fm",missing_capabilities:[]};
    if(path.endsWith("/sessions") && request.method()==="POST") {
      if(conflict) {await route.fulfill({status:409,json:{detail:{code:"lease_taken",scope:"console"}}});return calls;}
      data={lease_id:lease,expires_at:new Date(Date.now()+60000).toISOString()};
    }
    if(path.endsWith(`/operations/${lease}`))data={id:lease,lease_id:lease,kind:"session",state:"active",roots:["C:\\fixture"],entries:[]};
    if(path.endsWith("/operations") && request.method()==="POST")data={...request.postDataJSON(),state:"created"};
    if(path.includes("/operations/") && !path.endsWith(lease))data={id:path.split("/").pop(),lease_id:lease,kind:"list",state:"completed",entries:[{name:"report.txt",directory:false,size_bytes:3}]};
    await route.fulfill({json:data});
  });
  await page.goto(`/files?profile=${profile}`);
  return calls;
}

for(const profile of ["classic","l4desk"] as const) test(`FM is a standalone section in ${profile}`,async ({page})=>{
  const calls=await portal(page,profile);
  await expect(page.getByRole("menuitem",{name:"Файлы"})).toBeVisible();
  await expect(page.getByRole("button",{name:"Начать сеанс"})).toBeEnabled();
  expect(calls.some(call=>call.method==="POST")).toBe(false);
  await page.getByRole("button",{name:"Начать сеанс"}).click();
  await expect(page.getByText("Сессия активна",{exact:false})).toBeVisible();
  await page.getByRole("button",{name:"Открыть",exact:true}).click();
  await expect(page.getByText("report.txt",{exact:true})).toBeVisible();
  await page.getByRole("button",{name:"Завершить сеанс"}).click();
  await expect(page.getByRole("button",{name:"Начать сеанс"})).toBeVisible();
  expect(calls.some(call=>call.body?.action==="stop")).toBe(true);
});

test("incompatible agent refuses start without sending commands",async ({page})=>{
  const calls=await portal(page,"classic",false);
  await expect(page.getByText("Требуется обновление агента",{exact:true})).toBeVisible();
  await expect(page.getByRole("button",{name:"Начать сеанс"})).toBeDisabled();
  expect(calls.some(call=>call.method==="POST")).toBe(false);
});

test("occupied console shows refusal and never creates a file operation",async ({page})=>{
  const calls=await portal(page,"l4desk",true,true);
  await page.getByRole("button",{name:"Начать сеанс"}).click();
  await expect(page.getByText(/Терминал занят: консоль/)).toBeVisible();
  expect(calls.some(call=>call.path.endsWith("/operations") && call.method==="POST")).toBe(false);
});

test("failed start waits for safe drain before manual retry",async ({page})=>{
  const calls=await portal(page,"l4desk");
  await page.route("**/api/file-manager/v1/devices/10/sessions",async route=>{
    await route.fulfill({status:503,json:{detail:{code:"fm_start_failed",retry_after_sec:2}}});
  });
  await page.getByRole("button",{name:"Начать сеанс"}).click();
  await expect(page.getByText(/Не удалось начать сеанс FM/)).toBeVisible();
  await expect(page.getByText(/Ожидается безопасное завершение сеанса/)).toBeVisible();
  await expect(page.getByRole("button",{name:"Начать сеанс"})).toBeDisabled();
  await expect(page.getByRole("button",{name:"Начать сеанс"})).toBeEnabled({timeout:5000});
  expect(calls.some(call=>call.path.endsWith("/operations") && call.method==="POST")).toBe(false);
});

test("corrupt direct S3 download aborts the entire session without portal credentials",async ({page})=>{
  const calls=await portal(page,"classic");
  await page.getByRole("button",{name:"Начать сеанс"}).click();
  await expect(page.getByRole("button",{name:"Открыть",exact:true})).toBeEnabled();
  await page.getByRole("button",{name:"Открыть",exact:true}).click();
  await expect(page.getByText("report.txt",{exact:true})).toBeVisible();
  const operation="33333333-3333-4333-8333-333333333333";
  const lease="22222222-2222-4222-8222-222222222222";
  let storageHeaders: Record<string,string>={};
  await page.route("https://storage.example.invalid/**",async route=>{
    storageHeaders=await route.request().allHeaders();
    await route.fulfill({body:"bad",headers:{"Access-Control-Allow-Origin":"*"}});
  });
  await page.route("**/api/file-manager/v1/devices/10/operations**",async route=>{
    const path=new URL(route.request().url()).pathname;
    if(path.endsWith(`/operations/${lease}`)){await route.fallback();return;}
    if(path.endsWith("/operations")) {await route.fulfill({json:{id:operation,lease_id:lease,kind:"download",state:"created"}});return;}
    if(path.endsWith("/download")) {await route.fulfill({json:{url:"https://storage.example.invalid/object",headers:{},size_bytes:3,sha256:"0".repeat(64)}});return;}
    await route.fulfill({json:{id:operation,lease_id:lease,kind:"download",state:"verifying",size_bytes:3,sha256:"0".repeat(64)}});
  });
  await page.getByRole("button",{name:/Скачать/}).click();
  await expect(page.getByText(/Операция остановлена: не удалось подтвердить/)).toBeVisible();
  expect(storageHeaders.authorization).toBeUndefined();
  expect(storageHeaders.cookie).toBeUndefined();
  expect(storageHeaders["x-requested-with"]).toBeUndefined();
  expect(calls.some(call=>call.body?.action==="cancel")).toBe(true);
  expect(calls.some(call=>call.body?.action==="stop")).toBe(true);
});
