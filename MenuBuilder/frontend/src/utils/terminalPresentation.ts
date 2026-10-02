import type { DeviceListItem } from "../api/devices";
import type { TerminalSettingsItem } from "../api/settings";

export type TerminalStatus = "online" | "offline" | "disabled" | "unknown";
export type TerminalFilter = "all" | "online" | "offline" | "disabled";

export function terminalStatus(
  terminal: Pick<TerminalSettingsItem, "is_active">,
  device?: DeviceListItem,
): TerminalStatus {
  if (!terminal.is_active) return "disabled";
  if (!device?.connection) return "unknown";
  return device.status === "online" ? "online" : "offline";
}

export function filterTerminals(
  items: TerminalSettingsItem[],
  devices: Map<number, DeviceListItem>,
  filter: TerminalFilter,
): TerminalSettingsItem[] {
  return items.filter(
    (item) =>
      filter === "all" ||
      terminalStatus(item, devices.get(item.device_id)) === filter,
  );
}

export function certificatePresentation(
  terminal: Pick<TerminalSettingsItem, "cert_serial" | "cert_not_valid_after">,
  now = Date.now(),
) {
  if (!terminal.cert_serial)
    return {
      text: "Не активирован",
      color: "error",
      hint: "Сертификат ещё не установлен",
    };
  const expiry = terminal.cert_not_valid_after
    ? Date.parse(terminal.cert_not_valid_after)
    : NaN;
  if (!Number.isFinite(expiry))
    return {
      text: "Срок неизвестен",
      color: "default",
      hint: "Дата окончания сертификата недоступна",
    };
  const remaining = expiry - now;
  return {
    text: new Date(expiry).toLocaleDateString("ru-RU"),
    color:
      remaining <= 0
        ? "error"
        : remaining < 30 * 86400_000
          ? "warning"
          : "default",
    hint:
      remaining <= 0
        ? "Сертификат истёк"
        : `Действителен до ${new Date(expiry).toLocaleString("ru-RU")}`,
  };
}

/** The portal owns visibility; provider-only/deleted and inactive records cannot be selected. */
export function selectableDevices(
  devices: DeviceListItem[],
  terminals: TerminalSettingsItem[],
): DeviceListItem[] {
  const active = new Set(
    terminals.filter((item) => item.is_active).map((item) => item.device_id),
  );
  return devices
    .filter((item) => active.has(item.device_id))
    .sort((a, b) => a.device_id - b.device_id);
}
