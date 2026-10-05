import client from "./client";

export interface FmReadiness {
  state: string; write_available?: boolean; compatible: boolean; available: boolean; mqtt_available: boolean;
  agent_version?: string; protocol_version?: number; last_seen_at?: string;
  valid_until?: string; server_time: string; missing_capabilities: string[];
}
export interface FmEntry { name: string; directory: boolean; size_bytes: number }
export interface FmOperation {
  id: string; lease_id: string; kind: string; state: string; path: string;
  error_code?: string; expires_at: string; entries: FmEntry[];
  size_bytes?: number; sha256?: string; roots?: string[];
  applied_expires_at?: string;
  offset?: number; has_more?: boolean;
}
export interface FmGrant { url: string; headers: Record<string,string>; size_bytes: number; sha256: string }
export const MAX_FM_BYTES = 64 * 1024 * 1024;

export function fmApi(device: number, view: string, signal?: AbortSignal) {
  const base = `/file-manager/v1/devices/${device}`;
  const options = { headers: { "X-FM-View-Id": view }, signal, timeout: 10000 };
  return {
    readiness: async () => (await client.get<FmReadiness>(`${base}/readiness`, options)).data,
    start: async () => (await client.post<{ lease_id: string; expires_at: string }>(`${base}/sessions`, {}, options)).data,
    status: async (id: string) => (await client.get<FmOperation>(`${base}/operations/${id}`, options)).data,
    create: async (lease_id: string, kind: "list" | "upload" | "download", path: string, offset = 0) =>
      (await client.post<FmOperation>(`${base}/operations`, { id: crypto.randomUUID(), lease_id, kind, path, offset }, options)).data,
    signal: async (lease: string, action: "renew" | "stop" | "cancel", operation_id?: string) =>
      (await client.post(`${base}/sessions/${lease}/signals`, { action, operation_id }, options)).data as { expires_at: string },
    manifest: async (id: string, size_bytes: number, sha256: string) =>
      (await client.post<FmGrant>(`${base}/operations/${id}/manifest`, { size_bytes, sha256 }, options)).data,
    sourceComplete: async (id: string) => (await client.post(`${base}/operations/${id}/source-complete`, {}, options)).data,
    download: async (id: string) => (await client.get<FmGrant>(`${base}/operations/${id}/download`, options)).data,
    received: async (id: string, size_bytes: number, sha256: string) =>
      (await client.post(`${base}/operations/${id}/received`, { size_bytes, sha256 }, options)).data,
  };
}

export const fmReason: Record<string,string> = {
  storage_unavailable: "Хранилище файлов не настроено", policy_unconfigured: "Каталоги FM не настроены",
  ready: "Агент готов", not_registered: "Агент FM ещё не зарегистрирован",
  offline: "Нет свежего ответа агента", incompatible: "Требуется обновление агента",
  filesystem_unavailable: "Доступ к файловой системе отключён", disabled: "Терминал отключён",
  certificate_changed: "Ожидается регистрация нового сертификата", mqtt_unavailable: "Сервис агента недоступен по MQTT",
};

export function fmError(error: unknown): string {
  const detail = (error as { response?: { data?: { detail?: { code?: string; scope?: string } } } })?.response?.data?.detail;
  if (detail?.code === "lease_taken") return `Терминал занят: ${detail.scope === "files" ? "файловый менеджер" : detail.scope === "console" ? "консоль" : "видео или удалённое управление"}. Завершите другой сеанс.`;
  if (detail?.code === "fm_commit_outcome_unknown" || detail?.code === "fm_transfer_busy_or_commit_unknown") return "Результат записи пока неизвестен. Проверьте файл после восстановления связи.";
  return "Операция остановлена: не удалось подтвердить доставку или целостность. Проверьте состояние файла и начните заново.";
}

// The signed S3 URL receives no portal credentials or auth interceptors.
export async function fmStorage(grant: FmGrant, signal: AbortSignal, body?: File): Promise<Response> {
  if (new URL(grant.url).protocol !== "https:") throw new Error("HTTPS storage required");
  const response = await fetch(grant.url, { method: body ? "PUT" : "GET", headers: grant.headers,
    body, signal, credentials: "omit", redirect: "error", referrerPolicy: "no-referrer" });
  if (!response.ok) throw new Error("Storage transfer failed");
  return response;
}
