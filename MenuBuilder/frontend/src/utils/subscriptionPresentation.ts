import type { Subscription, UsageDay } from "../api/subscriptions";

export const subscriptionLabels: Record<Subscription["state"], string> = {
  free: "Бесплатно",
  active: "Оплачен",
  grace: "Нужно продлить",
  unpaid: "Требуется подписка",
  expired: "Подписка закончилась",
  payments_disabled: "Оплата пока недоступна",
  admin_disabled: "Отключён администратором",
  deleted: "Удалён",
};
export function canPurchase(terminal: Subscription, enabled: boolean) {
  return (
    enabled &&
    !terminal.is_free &&
    terminal.state !== "admin_disabled" &&
    terminal.state !== "deleted"
  );
}
export function usageTotals(rows: UsageDay[]) {
  return rows.reduce(
    (sum, day) => ({
      video: sum.video + day.video_seconds,
      console: sum.console + day.console_seconds,
    }),
    { video: 0, console: 0 },
  );
}
export function duration(seconds: number) {
  if (seconds < 60) return `${seconds} сек.`;
  return `${Math.floor(seconds / 3600)} ч. ${Math.floor((seconds % 3600) / 60)} мин.`;
}
export function usageByDay(rows: UsageDay[]) {
  const days = new Map<string, number>();
  for (const day of rows)
    days.set(
      day.date,
      (days.get(day.date) || 0) + day.video_seconds + day.console_seconds,
    );
  return [...days]
    .sort(([a], [b]) => a.localeCompare(b))
    .map(([date, seconds]) => ({ date, seconds }));
}
