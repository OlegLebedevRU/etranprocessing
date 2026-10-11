import type { OnboardTerminalPayload } from "../api/settings";

export const terminalKindOptions = [
  { value: "windows", label: "Windows" },
  { value: "linux", label: "Linux" },
  { value: "esp32", label: "ESP32" },
  { value: "camera-openipc", label: "Camera OpenIPC" },
];

export function terminalKindPayload(kind: string): Pick<OnboardTerminalPayload, "sys" | "camera"> {
  if (kind === "camera-openipc") return { sys: "esp32", camera: "openipc" };
  if (kind === "windows" || kind === "linux" || kind === "esp32") return { sys: kind };
  throw new Error("Неизвестный тип терминала");
}
