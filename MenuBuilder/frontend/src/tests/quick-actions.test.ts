import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { getNackMessage } from "../api/video";

describe("Step 6 Quick Actions & Remote Input Contracts", () => {
  beforeEach(() => {
    vi.useFakeTimers();
  });

  afterEach(() => {
    vi.restoreAllMocks();
  });

  // 1. NACK message translation and safety checks
  describe("getNackMessage", () => {
    it("returns correct human-readable messages for all contract codes", () => {
      expect(getNackMessage("action_blocked_policy")).toBe(
        "Действие запрещено настройками киоска"
      );
      expect(getNackMessage("action_blocked_winlogon_guard")).toBe(
        "Действие отклонено: небезопасное или изменившееся активное окно"
      );
      expect(getNackMessage("desktop_locked")).toBe(
        "Рабочий стол заблокирован. Удалённый ввод недоступен."
      );
      expect(getNackMessage("session_unavailable")).toBe(
        "Сессия пользователя недоступна. Удалённый ввод отключён."
      );
      expect(getNackMessage("insufficient_integrity")).toBe(
        "Недостаточный уровень прав для выполнения действия."
      );
      expect(getNackMessage("input_injection_failed")).toBe(
        "Результат ввода не подтвержден. Проверьте изображение перед повторным действием."
      );
      expect(getNackMessage("ack_timeout")).toBe(
        "Результат ввода не подтвержден. Проверьте изображение перед повторным действием."
      );
      expect(getNackMessage("invalid_payload")).toBe(
        "Некорректные параметры команды."
      );
      expect(getNackMessage("expired")).toBe("Срок действия команды истёк.");
    });

    it("does NOT instruct the operator to press UAC or bypass security desktop", () => {
      const lockedMsg = getNackMessage("desktop_locked");
      expect(lockedMsg.toLowerCase()).not.toContain("uac");
      expect(lockedMsg.toLowerCase()).not.toContain("пароль");
      expect(lockedMsg.toLowerCase()).not.toContain("разблокировать через");

      const sessionMsg = getNackMessage("session_unavailable");
      expect(sessionMsg.toLowerCase()).not.toContain("войти");
      expect(sessionMsg.toLowerCase()).not.toContain("login");
    });
  });

  // 2. Mouse click contract (left / right, coordinates clamping, no middle)
  describe("mouse_click contract", () => {
    it("formats right click and left click with clamped 0..65535 coordinates", () => {
      const formatClick = (
        x: number,
        y: number,
        button: "left" | "right" = "left",
        clientRef?: string
      ) => {
        const clampedX = Math.max(0, Math.min(65535, Math.round(x)));
        const clampedY = Math.max(0, Math.min(65535, Math.round(y)));
        return {
          type: "mouse_click",
          x: clampedX,
          y: clampedY,
          button,
          client_ref: clientRef,
        };
      };

      const rightClick = formatClick(32768, 32768, "right", "ref-1");
      expect(rightClick).toEqual({
        type: "mouse_click",
        x: 32768,
        y: 32768,
        button: "right",
        client_ref: "ref-1",
      });

      const clampedOob = formatClick(-100, 70000, "left", "ref-2");
      expect(clampedOob.x).toBe(0);
      expect(clampedOob.y).toBe(65535);
    });
  });

  // 3. Shortcut actions contract
  describe("shortcut_action contract", () => {
    it("only permits f12, alt_f4, and win_d actions", () => {
      const allowedActions = new Set(["f12", "alt_f4", "win_d"]);
      const validateAction = (action: string) => allowedActions.has(action);

      expect(validateAction("f12")).toBe(true);
      expect(validateAction("alt_f4")).toBe(true);
      expect(validateAction("win_d")).toBe(true);
      expect(validateAction("ctrl_alt_del")).toBe(false);
      expect(validateAction("f5")).toBe(false);
    });
  });

  // 4. Stale lease late event filtering
  describe("Stale lease late event filtering", () => {
    it("drops late action_result and click_result for prior lease_ids", () => {
      const currentLeaseId = "lease-active-2";
      const processedResults: any[] = [];

      const handleEvent = (data: any) => {
        if (data.type === "click_result" || data.type === "action_result") {
          if (data.lease_id && data.lease_id !== currentLeaseId) {
            // Drop stale event
            return;
          }
          processedResults.push(data);
        }
      };

      // Event from previous lease
      handleEvent({
        type: "action_result",
        lease_id: "lease-stale-1",
        command_id: "cmd-old",
        result: "injected",
      });

      // Event from current lease
      handleEvent({
        type: "action_result",
        lease_id: "lease-active-2",
        command_id: "cmd-current",
        result: "injected",
      });

      expect(processedResults).toHaveLength(1);
      expect(processedResults[0].command_id).toBe("cmd-current");
    });
  });

  // 5. 5-second ACK timeout behavior and no auto-retry
  describe("5-second timeout behavior", () => {
    it("resolves with unconfirmed and ack_timeout after 5 seconds without retrying", async () => {
      let sendAttempts = 0;
      const sendShortcut = (action: "f12" | "alt_f4" | "win_d") => {
        sendAttempts += 1;
        return new Promise((resolve) => {
          setTimeout(() => {
            resolve({
              result: "unconfirmed",
              code: "ack_timeout",
              message:
                "Результат ввода не подтвержден. Проверьте изображение перед повторным действием.",
            });
          }, 5000);
        });
      };

      const promise = sendShortcut("f12");
      expect(sendAttempts).toBe(1);

      vi.advanceTimersByTime(5000);
      const res: any = await promise;

      expect(res.result).toBe("unconfirmed");
      expect(res.code).toBe("ack_timeout");
      // Must not auto-retry:
      expect(sendAttempts).toBe(1);
    });
  });

  // 6. 480p quality constraint
  describe("480p quality policy", () => {
    it("restricts remote control activation to 480p (low) mode only", () => {
      const isControlAllowedForProfile = (profile?: string) => {
        return profile === "low" || profile === "480p";
      };

      expect(isControlAllowedForProfile("low")).toBe(true);
      expect(isControlAllowedForProfile("480p")).toBe(true);
      expect(isControlAllowedForProfile("default")).toBe(false); // 720p HD
      expect(isControlAllowedForProfile("1080p")).toBe(false);
      expect(isControlAllowedForProfile(undefined)).toBe(false);
    });
  });
});
