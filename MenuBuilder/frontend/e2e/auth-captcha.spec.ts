import { expect, test } from "@playwright/test";

test("login requires CAPTCHA and resets it after failed credentials", async ({ page }) => {
  const requests: unknown[] = [];
  await page.route("**/api/**", async route => {
    const path = new URL(route.request().url()).pathname;
    if (path === "/api/auth/captcha") return route.fulfill({ json: { enabled: true, site_key: "test-client-key" } });
    if (path === "/api/auth/register/status") return route.fulfill({ json: { enabled: true, terms_version: "v1", token_expire_hours: 24 } });
    if (path === "/api/auth/login") {
      requests.push(route.request().postDataJSON());
      return route.fulfill({ status: 401, json: { detail: "Invalid credentials" } });
    }
    return route.fulfill({ status: 401, json: { detail: "Unauthorized" } });
  });
  await page.route("https://smartcaptcha.cloud.yandex.ru/captcha.js?render=onload", route => route.fulfill({
    contentType: "application/javascript",
    body: `window.smartCaptcha = {
      render: (container, options) => {
        const button = document.createElement('button');
        button.type = 'button'; button.textContent = 'Пройти тестовую CAPTCHA';
        button.onclick = () => options.callback('mock-captcha-token');
        container.appendChild(button); window.captchaContainer = container; return 1;
      }, subscribe: () => () => {}, destroy: () => window.captchaContainer.replaceChildren()
    };`,
  }));
  await page.goto("/login");
  await page.getByPlaceholder("Логин").fill("user@example.test");
  await page.getByPlaceholder("Пароль", { exact: true }).fill("test-password");
  const submit = page.getByRole("button", { name: "Войти", exact: true });
  await expect(submit).toBeDisabled();
  await page.getByRole("button", { name: "Пройти тестовую CAPTCHA" }).click();
  await expect(submit).toBeEnabled();
  await submit.click();
  await expect(submit).toBeDisabled();
  await expect(page.getByRole("button", { name: "Пройти тестовую CAPTCHA" })).toBeVisible();
  expect(requests).toEqual([{ username: "user@example.test", password: "test-password", captcha_token: "mock-captcha-token" }]);
});

test("registration is closed when CAPTCHA configuration cannot load", async ({ page }) => {
  let refreshFailed = false;
  await page.route("**/api/**", async route => {
    const path = new URL(route.request().url()).pathname;
    if (path === "/api/auth/register/status") return route.fulfill({ json: { enabled: true, terms_version: "v1", token_expire_hours: 24 } });
    if (path === "/api/auth/me" || path === "/api/auth/refresh") {
      if (path === "/api/auth/refresh") refreshFailed = true;
      return route.fulfill({ status: 401, json: { detail: "Unauthorized" } });
    }
    return route.fulfill({ status: 503, json: { detail: "Unavailable" } });
  });
  await page.goto("/register");
  await expect(page.getByRole("button", { name: "Зарегистрироваться", exact: true })).toBeDisabled();
  await expect(page.getByText("Не удалось загрузить CAPTCHA. Проверьте соединение и повторите.")).toBeVisible();
  await expect(page.getByRole("button", { name: "Повторить", exact: true })).toBeVisible();
  await expect.poll(() => refreshFailed).toBe(true);
  await expect(page).toHaveURL(/\/register$/);
});

test("email confirmation remains public after anonymous refresh fails", async ({ page }) => {
  let refreshFailed = false;
  await page.route("**/api/**", async route => {
    if (new URL(route.request().url()).pathname === "/api/auth/refresh") refreshFailed = true;
    return route.fulfill({ status: 401, json: { detail: "Unauthorized" } });
  });
  await page.goto("/register/confirm");
  await expect.poll(() => refreshFailed).toBe(true);
  await expect(page).toHaveURL(/\/register\/confirm$/);
});
