import { describe, it, expect } from "vitest";
import {
  classifyRefusalReason,
  type RefusalReasonType,
} from "../components/RefusalReasonCard";

describe("L4Desk Rejection & Refusal Reason Classification", () => {
  it("Reason 1: Classifies session conflict correctly", () => {
    const res1 = classifyRefusalReason("session_busy");
    expect(res1.type).toBe("session_conflict");
    expect(res1.title).toBe("Конфликт активной сессии");
    expect(res1.badge).toBe("Конфликт сессии");
    expect(res1.color).toBe("warning");

    const res2 = classifyRefusalReason("conflict", "Терминал занят активной сессией");
    expect(res2.type).toBe("session_conflict");

    const res3 = classifyRefusalReason(undefined, "Автоматическое переключение запрещено для защиты оператора");
    expect(res3.type).toBe("session_conflict");
  });

  it("Reason 2: Classifies offline state correctly", () => {
    const res1 = classifyRefusalReason("offline");
    expect(res1.type).toBe("offline");
    expect(res1.title).toBe("Терминал не в сети (Offline)");
    expect(res1.badge).toBe("Не в сети");
    expect(res1.color).toBe("error");

    const res2 = classifyRefusalReason(undefined, "Device is offline. MQTT disconnected.");
    expect(res2.type).toBe("offline");
  });

  it("Reason 3: Classifies provisioning / certificate pending correctly", () => {
    const res1 = classifyRefusalReason("provisioning_pending");
    expect(res1.type).toBe("provisioning_pending");
    expect(res1.title).toBe("Подготовка терминала не завершена");
    expect(res1.badge).toBe("Ожидает настройки");
    expect(res1.color).toBe("info");

    const res2 = classifyRefusalReason("certificate_pending");
    expect(res2.type).toBe("provisioning_pending");

    const res3 = classifyRefusalReason(undefined, "Ожидается ввод PIN и выпуск сертификата");
    expect(res3.type).toBe("provisioning_pending");
  });

  it("Reason 4: Classifies free quota exhausted without paid access correctly", () => {
    const res1 = classifyRefusalReason("free_quota_exceeded");
    expect(res1.type).toBe("free_quota_exhausted");
    expect(res1.title).toBe("Бесплатная квота исчерпана");
    expect(res1.badge).toBe("Квота 120 мин исчерпана");
    expect(res1.color).toBe("warning");

    const res2 = classifyRefusalReason("unpaid_secondary_terminal");
    expect(res2.type).toBe("free_quota_exhausted");

    const res3 = classifyRefusalReason(undefined, "Бесплатная квота 120 минут исчерпана");
    expect(res3.type).toBe("free_quota_exhausted");
  });

  it("Reason 5: Classifies grace / blocked state correctly", () => {
    const res1 = classifyRefusalReason("entitlement_blocked");
    expect(res1.type).toBe("grace_blocked");
    expect(res1.title).toBe("Действие подписки приостановлено");
    expect(res1.badge).toBe("Блокировка подписки");
    expect(res1.color).toBe("error");

    const res2 = classifyRefusalReason("grace");
    expect(res2.type).toBe("grace_blocked");

    const res3 = classifyRefusalReason("payment_required");
    expect(res3.type).toBe("grace_blocked");

    const res4 = classifyRefusalReason(undefined, "Терминал заблокирован из-за неуплаты подписки");
    expect(res4.type).toBe("grace_blocked");
  });

  it("Falls back to unknown cleanly for generic errors", () => {
    const res = classifyRefusalReason("unknown_err", "Непредвиденная ошибка");
    expect(res.type).toBe("unknown");
    expect(res.title).toBe("Сессия отклонена");
    expect(res.description).toBe("Непредвиденная ошибка");
  });
});
