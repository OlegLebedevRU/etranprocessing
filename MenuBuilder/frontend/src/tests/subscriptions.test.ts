import { describe, expect, it, vi } from "vitest";
import {
  canPurchase,
  duration,
  subscriptionLabels,
  usageByDay,
  usageTotals,
} from "../utils/subscriptionPresentation";
import type { Subscription } from "../api/subscriptions";
vi.mock("../api/client", () => ({ default: { get: vi.fn(), post: vi.fn() } }));
import client from "../api/client";
import { createOrder, getQuote, getUsage } from "../api/subscriptions";

const terminal: Subscription = {
  terminal_id: 2,
  name: "Тест",
  state: "unpaid",
  is_free: false,
  allowed: false,
  paid_until: null,
  grace_until: null,
  reason: "Требуется подписка",
  action: "Подключить",
};

describe("Terminal subscriptions", () => {
  it("Offers payment only for eligible additional terminals with payments enabled", () => {
    expect(canPurchase(terminal, true)).toBe(true);
    expect(canPurchase(terminal, false)).toBe(false);
    expect(
      canPurchase({ ...terminal, is_free: true, state: "free" }, true),
    ).toBe(false);
    expect(canPurchase({ ...terminal, state: "admin_disabled" }, true)).toBe(
      false,
    );
    expect(subscriptionLabels.grace).toBe("Нужно продлить");
  });
  it("Aggregates technical durations without a free quota or monetary conversion", () => {
    const rows = [
      {
        terminal_id: 1,
        date: "2026-10-01",
        video_seconds: 9000,
        console_seconds: 30,
      },
      {
        terminal_id: 2,
        date: "2026-10-01",
        video_seconds: 0,
        console_seconds: 90,
      },
    ];
    expect(usageTotals(rows)).toEqual({ video: 9000, console: 120 });
    expect(usageByDay(rows)).toEqual([{ date: "2026-10-01", seconds: 9120 }]);
    expect(duration(30)).toBe("30 сек.");
    expect(duration(9120)).toBe("2 ч. 32 мин.");
  });
  it("Sends the reviewed basket and a stable operation key to the subscription API", async () => {
    vi.mocked(client.post).mockResolvedValue({ data: { id: 7 } });
    const items = [{ terminal_id: 2, months: 3 }];
    await getQuote(items);
    await createOrder(items, "server-quote", "stable-order-key");
    expect(client.post).toHaveBeenLastCalledWith("/subscriptions/orders", {
      items,
      quote_hash: "server-quote",
      operation_id: "stable-order-key",
    });
    vi.mocked(client.get).mockResolvedValue({ data: [] });
    await getUsage({ start: "2026-10-01", terminal_id: 2 });
    expect(client.get).toHaveBeenCalledWith("/usage", {
      params: { start: "2026-10-01", terminal_id: 2 },
    });
  });
});
