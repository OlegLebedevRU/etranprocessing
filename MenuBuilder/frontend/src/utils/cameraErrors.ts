const messages: Record<string, string> = {
  camera_writers_disabled: "Управление камерой выключено на сервере. Доступен просмотр; требуется включить управление.",
  ircut_contract_not_negotiated: "Терминал ещё не подтвердил версию диагностики IR-cut. Обновите состояние.",
  ircut_method_unsupported: "Проверка IR-cut не поддерживается этой прошивкой.",
  ircut_revision_conflict: "Камера изменилась после проверки. Перечитайте её состояние.",
  ircut_session_conflict: "Эта операция уже не текущая. Обновите состояние камеры.",
  ircut_camera_conflict: "Выбрана другая камера. Команда не выполнена.",
  camera_busy: "Сначала остановите просмотр. Чужая сессия не прерывается.",
  camera_terminal_offline: "Нет связи с терминалом.",
  camera_capability_unavailable: "Возможность камеры пока не подтверждена. Обновите состояние.",
  camera_revision_changed: "Настройки камеры изменились. Перечитайте их перед применением.",
};

export function cameraErrorMessage(error: unknown): string {
  const e = error as { response?: { status?: number; data?: { detail?: string | { code?: string; message?: string } } }; message?: string };
  const detail = e?.response?.data?.detail;
  const code = typeof detail === "string" ? detail : detail?.code;
  if (code && messages[code]) return messages[code];
  if (typeof detail === "object" && detail?.message) return detail.message;
  if (typeof detail === "string") return detail;
  if (e?.response?.status === 422) return "Камера или сервер не поддерживает выбранный параметр.";
  return e?.message || "Ответ не подтверждён. Автоматического повтора нет.";
}
