import { useEffect, useRef, useState } from "react";
import { Alert, Button, Spin } from "antd";
import { getCaptchaConfig } from "../api/auth";

interface CaptchaApi {
  render(container: HTMLElement, options: { sitekey: string; hl: string; callback: (token: string) => void }): number;
  subscribe(id: number, event: string, callback: () => void): () => void;
  destroy(id: number): void;
}
declare global { interface Window { smartCaptcha?: CaptchaApi } }

let scriptPromise: Promise<CaptchaApi> | undefined;
function loadCaptcha(): Promise<CaptchaApi> {
  if (window.smartCaptcha) return Promise.resolve(window.smartCaptcha);
  if (!scriptPromise) {
    scriptPromise = new Promise<CaptchaApi>((resolve, reject) => {
      const script = document.createElement("script");
      script.src = "https://smartcaptcha.cloud.yandex.ru/captcha.js?render=onload";
      script.async = true;
      const timer = window.setTimeout(() => fail(), 15000);
      const fail = () => {
        window.clearTimeout(timer);
        script.remove();
        scriptPromise = undefined;
        reject(new Error("CAPTCHA unavailable"));
      };
      script.onerror = fail;
      script.onload = () => {
        window.clearTimeout(timer);
        if (window.smartCaptcha) resolve(window.smartCaptcha);
        else fail();
      };
      document.head.appendChild(script);
    });
  }
  return scriptPromise;
}

export function AuthCaptcha({ onChange }: { onChange: (token: string | null) => void }) {
  const container = useRef<HTMLDivElement>(null);
  const [status, setStatus] = useState<"loading" | "ready" | "error" | "disabled">("loading");
  const [attempt, setAttempt] = useState(0);
  useEffect(() => {
    let cancelled = false;
    let cleanup: (() => void) | undefined;
    onChange(null);
    setStatus("loading");
    void (async () => {
      try {
        const config = await getCaptchaConfig();
        if (cancelled) return;
        if (!config.enabled) { setStatus("disabled"); onChange(""); return; }
        if (!config.site_key) throw new Error("CAPTCHA unconfigured");
        const api = await loadCaptcha();
        if (cancelled || !container.current) return;
        const id = api.render(container.current, {
          sitekey: config.site_key, hl: "ru",
          callback: (token) => { if (!cancelled) onChange(token || null); },
        });
        const subscriptions = [
          api.subscribe(id, "token-expired", () => onChange(null)),
          ...["network-error", "javascript-error"].map(event => api.subscribe(id, event, () => {
            onChange(null); setStatus("error");
          })),
        ];
        cleanup = () => { subscriptions.forEach(unsubscribe => unsubscribe()); api.destroy(id); };
        setStatus("ready");
      } catch { if (!cancelled) { onChange(null); setStatus("error"); } }
    })();
    return () => { cancelled = true; cleanup?.(); };
  }, [onChange, attempt]);
  return <div style={{ marginBottom: 16 }}>
    <div ref={container} style={{ display: status === "disabled" ? "none" : "block", minHeight: status === "disabled" ? 0 : 100 }} />
    {status === "loading" && <Spin size="small" tip="Загрузка CAPTCHA" />}
    {status === "error" && <Alert type="error" showIcon message="Не удалось загрузить CAPTCHA. Проверьте соединение и повторите."
      action={<Button size="small" onClick={() => setAttempt(value => value + 1)}>Повторить</Button>} />}
  </div>;
}
