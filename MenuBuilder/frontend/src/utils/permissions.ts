import { UserInfo } from "../api/auth";

export const PERMISSION_MONITORING_VIEW = "monitoring:view";
export const PERMISSION_REPORTS_INKASS_VIEW = "reports:inkass:view";
export const PERMISSION_REPORTS_PAYMENTS_VIEW = "reports:payments:view";
export const PERMISSION_REPORTS_BALANCE_TERMINAL_VIEW = "reports:balance_terminal:view";
export const PERMISSION_REPORTS_BALANCE_TSP_VIEW = "reports:balance_tsp:view";
export const PERMISSION_REPORTS_EXPORT = "reports:export";
export const PERMISSION_BILLING_VIEW = "billing:view";
export const PERMISSION_SETTINGS_TERMINALS_VIEW = "settings:terminals:view";

export const ALL_PERMISSIONS = [
  PERMISSION_MONITORING_VIEW,
  PERMISSION_REPORTS_INKASS_VIEW,
  PERMISSION_REPORTS_PAYMENTS_VIEW,
  PERMISSION_REPORTS_BALANCE_TERMINAL_VIEW,
  PERMISSION_REPORTS_BALANCE_TSP_VIEW,
  PERMISSION_REPORTS_EXPORT,
  PERMISSION_BILLING_VIEW,
  PERMISSION_SETTINGS_TERMINALS_VIEW,
] as const;

export const PERMISSION_LABELS: Record<string, string> = {
  [PERMISSION_MONITORING_VIEW]: "Мониторинг",
  [PERMISSION_REPORTS_INKASS_VIEW]: "Отчёт: Инкассация",
  [PERMISSION_REPORTS_PAYMENTS_VIEW]: "Отчёт: Платежи",
  [PERMISSION_REPORTS_BALANCE_TERMINAL_VIEW]: "Отчёт: По терминалам",
  [PERMISSION_REPORTS_BALANCE_TSP_VIEW]: "Отчёт: По ТСП",
  [PERMISSION_REPORTS_EXPORT]: "Экспорт отчетов в файл",
  [PERMISSION_BILLING_VIEW]: "Лицензии (просмотр)",
  [PERMISSION_SETTINGS_TERMINALS_VIEW]: "Терминалы (настройки)",
};

export function hasPermission(user: UserInfo | null, permissionCode: string): boolean {
  if (!user) return false;
  // Роли 1, 2, 3 имеют полный доступ
  if (user.is_superuser || user.role_id === 1 || user.role_id === 2 || user.role_id === 3) {
    return true;
  }
  // Роль 4 проверяет наличие права в permissions
  if (user.role_id === 4 && Array.isArray(user.permissions)) {
    return user.permissions.includes(permissionCode) || user.permissions.includes("*");
  }
  return false;
}

export function hasAnyReportPermission(user: UserInfo | null): boolean {
  if (!user) return false;
  if (user.is_superuser || user.role_id === 1 || user.role_id === 2 || user.role_id === 3) {
    return true;
  }
  return (
    hasPermission(user, PERMISSION_REPORTS_INKASS_VIEW) ||
    hasPermission(user, PERMISSION_REPORTS_PAYMENTS_VIEW) ||
    hasPermission(user, PERMISSION_REPORTS_BALANCE_TERMINAL_VIEW) ||
    hasPermission(user, PERMISSION_REPORTS_BALANCE_TSP_VIEW)
  );
}

export function getDefaultRouteForViewer(user: UserInfo | null): string | null {
  if (!user) return null;
  if (user.role_id !== 4) return "/monitoring";
  if (hasPermission(user, PERMISSION_MONITORING_VIEW)) return "/monitoring";
  if (hasPermission(user, PERMISSION_REPORTS_PAYMENTS_VIEW)) return "/reports?tab=payments";
  if (hasPermission(user, PERMISSION_REPORTS_INKASS_VIEW)) return "/reports?tab=inkass";
  if (hasPermission(user, PERMISSION_REPORTS_BALANCE_TERMINAL_VIEW)) return "/reports?tab=balance-terminal";
  if (hasPermission(user, PERMISSION_REPORTS_BALANCE_TSP_VIEW)) return "/reports?tab=balance-tsp";
  if (hasPermission(user, PERMISSION_BILLING_VIEW)) return "/billing";
  if (hasPermission(user, PERMISSION_SETTINGS_TERMINALS_VIEW)) return "/settings/terminals";
  return null;
}
