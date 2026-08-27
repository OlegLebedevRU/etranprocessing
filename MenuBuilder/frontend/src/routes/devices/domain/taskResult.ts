import { TaskStatus, type TaskDetail, type TaskResultItem } from "./types";

export type TaskExecutionState =
  | "ready"
  | "pending"
  | "locked"
  | "success"
  | "timeout"
  | "deleted"
  | "failed"
  | "unknown";

export interface TaskStatusInfo {
  status: TaskStatus;
  label: string;
  shortLabel: string;
  state: TaskExecutionState;
  color: "processing" | "success" | "warning" | "default" | "error";
  isFinished: boolean;
  isSuccess: boolean;
  isError: boolean;
  isInProgress: boolean;
}

/**
 * Получить подробную информацию о статусе задачи
 */
export function getTaskStatusInfo(status: number | TaskStatus): TaskStatusInfo {
  const numStatus = Number(status);

  switch (numStatus) {
    case TaskStatus.READY:
      return {
        status: TaskStatus.READY,
        label: "Готова к отправке",
        shortLabel: "Готова",
        state: "ready",
        color: "processing",
        isFinished: false,
        isSuccess: false,
        isError: false,
        isInProgress: true,
      };
    case TaskStatus.PENDING:
      return {
        status: TaskStatus.PENDING,
        label: "Ожидает устройство (ACK)",
        shortLabel: "Ожидает",
        state: "pending",
        color: "processing",
        isFinished: false,
        isSuccess: false,
        isError: false,
        isInProgress: true,
      };
    case TaskStatus.LOCK:
      return {
        status: TaskStatus.LOCK,
        label: "Выполняется на устройстве",
        shortLabel: "Выполняется",
        state: "locked",
        color: "processing",
        isFinished: false,
        isSuccess: false,
        isError: false,
        isInProgress: true,
      };
    case TaskStatus.DONE:
      return {
        status: TaskStatus.DONE,
        label: "Успешно выполнено",
        shortLabel: "Выполнено",
        state: "success",
        color: "success",
        isFinished: true,
        isSuccess: true,
        isError: false,
        isInProgress: false,
      };
    case TaskStatus.EXPIRED:
      return {
        status: TaskStatus.EXPIRED,
        label: "Истекло время жизни (TTL таймаут)",
        shortLabel: "Таймаут",
        state: "timeout",
        color: "warning",
        isFinished: true,
        isSuccess: false,
        isError: true,
        isInProgress: false,
      };
    case TaskStatus.DELETED:
      return {
        status: TaskStatus.DELETED,
        label: "Отменена / Удалена",
        shortLabel: "Отменена",
        state: "deleted",
        color: "default",
        isFinished: true,
        isSuccess: false,
        isError: false,
        isInProgress: false,
      };
    case TaskStatus.FAILED:
      return {
        status: TaskStatus.FAILED,
        label: "Ошибка выполнения",
        shortLabel: "Ошибка",
        state: "failed",
        color: "error",
        isFinished: true,
        isSuccess: false,
        isError: true,
        isInProgress: false,
      };
    default:
      return {
        status: TaskStatus.UNDEFINED,
        label: `Неопределенный статус (${numStatus})`,
        shortLabel: "Неизвестно",
        state: "unknown",
        color: "default",
        isFinished: false,
        isSuccess: false,
        isError: false,
        isInProgress: false,
      };
  }
}

export function isTaskFinished(status: number | TaskStatus): boolean {
  return getTaskStatusInfo(status).isFinished;
}

export function isTaskSuccess(status: number | TaskStatus): boolean {
  return getTaskStatusInfo(status).isSuccess;
}

export function isTaskInProgress(status: number | TaskStatus): boolean {
  return getTaskStatusInfo(status).isInProgress;
}

export interface ExtractedTaskResults {
  hasResults: boolean;
  statusCode?: number;
  resultsList: TaskResultItem[];
  primaryResult?: Record<string, any> | any[] | null;
  summary: string;
}

/**
 * Извлечение и нормализация данных результатов RPC-вызова
 */
export function extractTaskResults(task: Partial<TaskDetail> | null | undefined): ExtractedTaskResults {
  if (!task) {
    return {
      hasResults: false,
      resultsList: [],
      summary: "Нет данных задачи",
    };
  }

  const results = task.results;

  if (Array.isArray(results) && results.length > 0) {
    const firstResult = results[0];
    const statusCode = firstResult?.status_code;
    const primaryResult = firstResult?.result ?? null;

    let summary = `Код ответа: ${statusCode !== undefined ? statusCode : "—"}`;
    if (primaryResult && typeof primaryResult === "object") {
      summary += ` | Данные: ${JSON.stringify(primaryResult)}`;
    }

    return {
      hasResults: true,
      statusCode,
      resultsList: results,
      primaryResult,
      summary,
    };
  }

  // Если results не массив, но есть объект results или params
  if (results && typeof results === "object") {
    return {
      hasResults: true,
      resultsList: [],
      primaryResult: results,
      summary: JSON.stringify(results),
    };
  }

  const statusInfo = getTaskStatusInfo(task.status ?? TaskStatus.READY);
  return {
    hasResults: false,
    resultsList: [],
    summary: statusInfo.isInProgress
      ? "Ожидается ответ от устройства..."
      : `Результат отсутствует (${statusInfo.label})`,
  };
}

/**
 * Безопасное форматирование временной метки (секунды, миллисекунды, ISO-строка)
 */
export function formatTimestamp(ts: number | string | undefined | null): string {
  if (!ts) return "—";

  let date: Date;
  if (typeof ts === "number") {
    // Если секунды (10 знаков) -> переводим в миллисекунды
    date = ts < 10000000000 ? new Date(ts * 1000) : new Date(ts);
  } else if (!Number.isNaN(Number(ts))) {
    const num = Number(ts);
    date = num < 10000000000 ? new Date(num * 1000) : new Date(num);
  } else {
    date = new Date(ts);
  }

  if (Number.isNaN(date.getTime())) {
    return String(ts);
  }

  return date.toLocaleString("ru-RU", {
    year: "numeric",
    month: "2-digit",
    day: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
  });
}
