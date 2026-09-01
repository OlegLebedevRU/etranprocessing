import dayjs from "dayjs";
import utc from "dayjs/plugin/utc";
import timezone from "dayjs/plugin/timezone";

dayjs.extend(utc);
dayjs.extend(timezone);

export interface TimezoneOption {
  value: string;
  label: string;
  city: string;
  offset: string;
  regions: string;
}

export const TIMEZONE_OPTIONS: TimezoneOption[] = [
  {
    value: "Europe/Kaliningrad",
    label: "Калининград (UTC+2)",
    city: "Калининград",
    offset: "UTC+2",
    regions: "Калининградская область",
  },
  {
    value: "Europe/Moscow",
    label: "Москва, Санкт-Петербург (UTC+3)",
    city: "Москва, Санкт-Петербург",
    offset: "UTC+3",
    regions: "Москва, СПб, Центр, Юг, Поволжье",
  },
  {
    value: "Europe/Samara",
    label: "Самара, Ижевск (UTC+4)",
    city: "Самара, Ижевск",
    offset: "UTC+4",
    regions: "Самарская, Удмуртия, Астрахань",
  },
  {
    value: "Asia/Yekaterinburg",
    label: "Екатеринбург, Тюмень (UTC+5)",
    city: "Екатеринбург, Тюмень",
    offset: "UTC+5",
    regions: "Тюмень, Екатеринбург, ХМАО, ЯНАО, Пермь, Челябинск",
  },
  {
    value: "Asia/Omsk",
    label: "Омск (UTC+6)",
    city: "Омск",
    offset: "UTC+6",
    regions: "Омская область",
  },
  {
    value: "Asia/Krasnoyarsk",
    label: "Красноярск, Новосибирск (UTC+7)",
    city: "Красноярск, Новосибирск",
    offset: "UTC+7",
    regions: "Красноярск, Новосибирск, Томск, Кузбасс, Алтай",
  },
  {
    value: "Asia/Irkutsk",
    label: "Иркутск (UTC+8)",
    city: "Иркутск",
    offset: "UTC+8",
    regions: "Иркутская область, Бурятия",
  },
  {
    value: "Asia/Yakutsk",
    label: "Якутск, Чита (UTC+9)",
    city: "Якутск, Чита",
    offset: "UTC+9",
    regions: "Якутия (центр), Забайкальский край",
  },
  {
    value: "Asia/Vladivostok",
    label: "Владивосток, Хабаровск (UTC+10)",
    city: "Владивосток, Хабаровск",
    offset: "UTC+10",
    regions: "Приморский, Хабаровский край",
  },
  {
    value: "Asia/Magadan",
    label: "Магадан, Сахалин (UTC+11)",
    city: "Магадан, Сахалин",
    offset: "UTC+11",
    regions: "Магадан, Сахалинская область",
  },
  {
    value: "Asia/Kamchatka",
    label: "Камчатка, Чукотка (UTC+12)",
    city: "Камчатка, Чукотка",
    offset: "UTC+12",
    regions: "Камчатский край, Чукотский АО",
  },
];

export const DEFAULT_TIMEZONE = "Europe/Moscow";

const STORAGE_KEY = "org_timezone";

export function getStoredTenantTimezone(): string {
  try {
    return localStorage.getItem(STORAGE_KEY) || DEFAULT_TIMEZONE;
  } catch {
    return DEFAULT_TIMEZONE;
  }
}

export function setStoredTenantTimezone(tz: string): void {
  try {
    if (tz) {
      localStorage.setItem(STORAGE_KEY, tz);
    }
  } catch {
    // Ignore storage errors in private browsing/restricted environments
  }
}

export function resolveTenantTimezone(explicitTz?: string | null): string {
  return explicitTz || getStoredTenantTimezone() || DEFAULT_TIMEZONE;
}

export function getTimezoneOption(tz?: string | null): TimezoneOption | undefined {
  if (!tz) return undefined;
  return TIMEZONE_OPTIONS.find((opt) => opt.value === tz);
}

export function getTimezoneLabel(tz?: string | null): string {
  if (!tz) return "По умолчанию (организация)";
  const opt = getTimezoneOption(tz);
  return opt ? opt.label : tz;
}

export function getTimezoneBadgeText(tz?: string | null): string {
  const resolved = resolveTenantTimezone(tz);
  const opt = getTimezoneOption(resolved);
  if (opt) {
    return `${resolved} (${opt.offset})`;
  }
  return resolved;
}

/**
 * Format any date/time string, timestamp or Date object into tenant's timezone.
 *
 * @param dateStr - ISO string, timestamp or date
 * @param tz - Target timezone (defaults to stored tenant timezone)
 * @param format - Dayjs format string (default: "YYYY-MM-DD HH:mm:ss")
 */
export function formatTenantDateTime(
  dateStr?: string | number | Date | null,
  tz?: string | null,
  format = "YYYY-MM-DD HH:mm:ss"
): string {
  if (!dateStr) return "—";
  try {
    const targetTz = resolveTenantTimezone(tz);
    const d = dayjs(dateStr);
    if (!d.isValid()) return String(dateStr);
    return d.tz(targetTz).format(format);
  } catch {
    return String(dateStr);
  }
}

/**
 * Format date part only in tenant timezone (default: "DD.MM.YYYY").
 */
export function formatTenantDate(
  dateStr?: string | number | Date | null,
  tz?: string | null,
  format = "DD.MM.YYYY"
): string {
  return formatTenantDateTime(dateStr, tz, format);
}

export { dayjs };
