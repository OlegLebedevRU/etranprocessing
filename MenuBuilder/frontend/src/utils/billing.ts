/**
 * Formatting utilities for billing display.
 */

/**
 * Format minor units (kopecks) to display currency, dropping kopecks entirely.
 * 300000 → "3 000 ₽", 30050 → "300 ₽"
 */
export function formatMoneyMinor(value: number, currency = "RUB"): string {
  const major = Math.trunc(value / 100);
  return new Intl.NumberFormat("ru-RU", {
    style: "currency",
    currency,
    minimumFractionDigits: 0,
    maximumFractionDigits: 0,
  }).format(major);
}

/**
 * Format debt amount with minus sign (for display).
 * 300000 → "−3 000 ₽"
 */
export function formatDebt(value: number, currency = "RUB"): string {
  if (value <= 0) return formatMoneyMinor(0, currency);
  return `−${formatMoneyMinor(value, currency)}`;
}

/**
 * Format ISO datetime to Russian date string.
 * "2026-08-10T00:00:00Z" → "10.08.2026"
 */
export function formatDate(value: string | null): string {
  if (!value) return "—";
  const d = new Date(value);
  return d.toLocaleDateString("ru-RU", {
    day: "2-digit",
    month: "2-digit",
    year: "numeric",
  });
}

/**
 * Format billing period months to Russian text.
 * 1 → "1 месяц", 2 → "2 месяца", 5 → "5 месяцев"
 */
export function formatBillingPeriod(months: number): string {
  const mod10 = months % 10;
  const mod100 = months % 100;

  if (mod100 >= 11 && mod100 <= 19) return `${months} месяцев`;
  if (mod10 === 1) return `${months} месяц`;
  if (mod10 >= 2 && mod10 <= 4) return `${months} месяца`;
  return `${months} месяцев`;
}

/**
 * Format forecast month key to display.
 * "2026-08" → "Август 2026"
 */
export function formatForecastMonth(month: string): string {
  const [year, m] = month.split("-");
  const monthNames = [
    "Январь", "Февраль", "Март", "Апрель", "Май", "Июнь",
    "Июль", "Август", "Сентябрь", "Октябрь", "Ноябрь", "Декабрь",
  ];
  return `${monthNames[parseInt(m, 10) - 1]} ${year}`;
}

/**
 * Get billing status display label.
 */
export function billingStatusLabel(status: string): string {
  const labels: Record<string, string> = {
    active: "Оплачен",
    due_soon: "Истекает",
    overdue: "Требует оплаты",
    deactivation_scheduled: "Отключен",
    disabled: "Отключен",
    admin_disabled: "Отключен",
    no_license: "Нет лицензии",
  };
  return labels[status] || status;
}

/**
 * Get billing status color for Ant Design Tag.
 */
export function billingStatusColor(status: string): string {
  const colors: Record<string, string> = {
    active: "success",
    due_soon: "warning",
    overdue: "error",
    deactivation_scheduled: "default",
    disabled: "default",
    admin_disabled: "default",
    no_license: "default",
  };
  return colors[status] || "default";
}
