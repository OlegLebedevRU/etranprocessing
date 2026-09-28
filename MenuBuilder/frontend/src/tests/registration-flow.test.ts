import { describe, it, expect, vi, beforeEach } from "vitest";

// Pure helper function modeling return url sanitization on frontend
export function sanitizeReturnUrl(rawUrl: string | null | undefined): string {
  const defaultUrl = "/monitoring";
  if (!rawUrl) return defaultUrl;
  const trimmed = rawUrl.trim();
  if (!trimmed) return defaultUrl;
  // Disallow protocol-relative and backslash injections
  if (trimmed.startsWith("//") || trimmed.startsWith("/\\") || trimmed.startsWith("\\")) {
    return defaultUrl;
  }
  // Allow safe relative paths
  if (trimmed.startsWith("/")) {
    if (/[\r\n\t\0]/.test(trimmed)) return defaultUrl;
    return trimmed;
  }
  // Allow whitelisted origins
  try {
    const parsed = new URL(trimmed);
    if (["http:", "https:"].includes(parsed.protocol)) {
      const allowedHosts = ["localhost", "127.0.0.1", "leo4.ru"];
      if (allowedHosts.includes(parsed.hostname.toLowerCase())) {
        return trimmed;
      }
    }
  } catch {
    // Malformed URL
  }
  return defaultUrl;
}

export type RegistrationConfirmState =
  | "missing_token"
  | "verifying"
  | "success"
  | "already_confirmed"
  | "expired"
  | "invalid"
  | "error";

export class RegistrationConfirmController {
  public state: RegistrationConfirmState = "verifying";
  public result: any = null;
  public errorMessage: string | null = null;

  constructor(
    private readonly confirmApi: (payload: { token: string; return_url?: string }) => Promise<any>
  ) {}

  public async init(token: string | null | undefined, rawReturnUrl?: string | null) {
    if (!token || !token.trim()) {
      this.state = "missing_token";
      return;
    }
    const safeReturnUrl = sanitizeReturnUrl(rawReturnUrl);
    this.state = "verifying";
    try {
      const res = await this.confirmApi({ token: token.trim(), return_url: safeReturnUrl });
      this.result = res;
      if (res.status === "already_confirmed") {
        this.state = "already_confirmed";
      } else {
        this.state = "success";
      }
    } catch (err: any) {
      const detail =
        err?.response?.data?.detail || err?.response?.data?.message || err?.message || "";
      if (detail.includes("token_expired") || detail.toLowerCase().includes("истек")) {
        this.state = "expired";
      } else if (
        detail.includes("invalid_token") ||
        detail.toLowerCase().includes("неверный") ||
        detail.toLowerCase().includes("не существует")
      ) {
        this.state = "invalid";
      } else {
        this.state = "error";
        this.errorMessage = detail || "Ошибка подтверждения";
      }
    }
  }
}

describe("L4Desk Registration & Confirmation State Transitions", () => {
  let mockConfirmApi: any;

  beforeEach(() => {
    mockConfirmApi = vi.fn();
  });

  it("Transition to missing_token when token parameter is empty or missing", async () => {
    const controller = new RegistrationConfirmController(mockConfirmApi);
    await controller.init(null);
    expect(controller.state).toBe("missing_token");
    expect(mockConfirmApi).not.toHaveBeenCalled();

    await controller.init("   ");
    expect(controller.state).toBe("missing_token");
  });

  it("Transition to success when API confirms new tenant registration", async () => {
    mockConfirmApi.mockResolvedValue({
      status: "confirmed",
      message: "Email успешно подтвержден.",
      tenant_id: 101,
      user_id: 501,
      email: "new.owner@leo4.ru",
      return_url: "/monitoring",
    });

    const controller = new RegistrationConfirmController(mockConfirmApi);
    await controller.init("valid_raw_token_123");

    expect(controller.state).toBe("success");
    expect(controller.result.tenant_id).toBe(101);
    expect(controller.result.user_id).toBe(501);
  });

  it("Transition to already_confirmed for idempotent replays", async () => {
    mockConfirmApi.mockResolvedValue({
      status: "already_confirmed",
      message: "Регистрация уже была подтверждена.",
      tenant_id: 101,
      user_id: 501,
    });

    const controller = new RegistrationConfirmController(mockConfirmApi);
    await controller.init("already_used_token");

    expect(controller.state).toBe("already_confirmed");
    expect(controller.result.tenant_id).toBe(101);
  });

  it("Transition to expired when token lifetime exceeded", async () => {
    mockConfirmApi.mockRejectedValue({
      response: {
        status: 400,
        data: { detail: "token_expired: Срок действия ссылки истек" },
      },
    });

    const controller = new RegistrationConfirmController(mockConfirmApi);
    await controller.init("expired_token");

    expect(controller.state).toBe("expired");
  });

  it("Transition to invalid when token is unknown or corrupt", async () => {
    mockConfirmApi.mockRejectedValue({
      response: {
        status: 400,
        data: { detail: "invalid_token: Неверный токен" },
      },
    });

    const controller = new RegistrationConfirmController(mockConfirmApi);
    await controller.init("corrupt_token");

    expect(controller.state).toBe("invalid");
  });

  it("Sanitizes malicious return_url values to prevent open redirects", () => {
    expect(sanitizeReturnUrl("/settings/terminals")).toBe("/settings/terminals");
    expect(sanitizeReturnUrl("//attacker.com")).toBe("/monitoring");
    expect(sanitizeReturnUrl("//attacker.com/steal")).toBe("/monitoring");
    expect(sanitizeReturnUrl("/\\attacker.com")).toBe("/monitoring");
    expect(sanitizeReturnUrl("\\\\attacker.com")).toBe("/monitoring");
    expect(sanitizeReturnUrl("javascript:alert(document.cookie)")).toBe("/monitoring");
    expect(sanitizeReturnUrl("https://evil.com/login")).toBe("/monitoring");
    expect(sanitizeReturnUrl("https://leo4.ru/monitoring")).toBe("https://leo4.ru/monitoring");
  });
});
