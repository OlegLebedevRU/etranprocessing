import type { MethodDefinition, TaskCreatePayload, NvsType } from "./types";
import { CUSTOM_METHOD_CODE } from "./methodCodes";

/**
 * Генератор 20-значного случайного идентификатора задачи (a-z0-9)
 */
export function generateExtTaskId(length = 20): string {
  const chars = "abcdefghijklmnopqrstuvwxyz0123456789";
  let result = "";
  if (typeof crypto !== "undefined" && crypto.getRandomValues) {
    const randomBytes = new Uint8Array(length);
    crypto.getRandomValues(randomBytes);
    for (let i = 0; i < length; i++) {
      result += chars[randomBytes[i] % chars.length];
    }
  } else {
    for (let i = 0; i < length; i++) {
      result += chars.charAt(Math.floor(Math.random() * chars.length));
    }
  }
  return result;
}

/**
 * Валидация и нормализация значения для NVS-записи
 */
export function validateAndFormatNvsValue(type: NvsType, rawVal: any): string | number {
  const strVal = String(rawVal ?? "").trim();
  if (type === "str") {
    return strVal;
  }

  const num = Number(strVal);
  if (Number.isNaN(num) || !Number.isInteger(num)) {
    throw new Error(`Значение '${strVal}' должно быть целым числом для типа ${type}`);
  }

  switch (type) {
    case "i8":
      if (num < -128 || num > 127) throw new Error(`Тип i8 требует диапазон -128..127 (получено ${num})`);
      break;
    case "u8":
      if (num < 0 || num > 255) throw new Error(`Тип u8 требует диапазон 0..255 (получено ${num})`);
      break;
    case "i16":
      if (num < -32768 || num > 32767) throw new Error(`Тип i16 требует диапазон -32768..32767 (получено ${num})`);
      break;
    case "u16":
      if (num < 0 || num > 65535) throw new Error(`Тип u16 требует диапазон 0..65535 (получено ${num})`);
      break;
    case "i32":
      if (num < -2147483648 || num > 2147483647) throw new Error(`Тип i32 требует диапазон -2147483648..2147483647 (получено ${num})`);
      break;
    case "u32":
      if (num < 0 || num > 4294967295) throw new Error(`Тип u32 требует диапазон 0..4294967295 (получено ${num})`);
      break;
  }
  return num;
}

/**
 * Сборка массива полезной нагрузки payload.dt по формату метода
 */
export function buildPayloadDt(method: MethodDefinition, formValues: any): any[] {
  if (!formValues) {
    return [];
  }

  switch (method.dtFormat) {
    case "empty":
      return [];

    case "stringArray": {
      // Метод 35: Ввод PIN-кода -> ["123456"]
      const pin = formValues.pin !== undefined ? String(formValues.pin).trim() : "";
      return pin ? [pin] : [];
    }

    case "numberArray": {
      // Метод 47: Удаление привязок -> [1, 2, 5]
      if (Array.isArray(formValues.slots)) {
        return formValues.slots.map(Number).filter((n: number) => !Number.isNaN(n));
      }
      if (typeof formValues.slots === "string" && formValues.slots.trim()) {
        return formValues.slots
          .split(/[,;\s]+/)
          .map((s: string) => s.trim())
          .filter(Boolean)
          .map(Number)
          .filter((n: number) => !Number.isNaN(n));
      }
      return [];
    }

    case "objectFields": {
      // Метод 512: Загрузка прошивки -> [{ url, sha256, chunk_size }]
      const item: Record<string, any> = {};
      if (formValues.url) item.url = String(formValues.url).trim();
      if (formValues.sha256) item.sha256 = String(formValues.sha256).trim();
      if (formValues.chunk_size) item.chunk_size = Number(formValues.chunk_size);
      return Object.keys(item).length > 0 ? [item] : [];
    }

    case "fullscreenJpeg": {
      // Метод 7010: Полноэкранный JPEG -> [{ action: "show", url }] или [{ action: "hide" }]
      const action = formValues.action || "show";
      if (action === "hide") {
        return [{ action: "hide" }];
      }
      return [{ action: "show", url: String(formValues.url || "").trim() }];
    }

    case "dbWrite": {
      // Метод 49: Запись в NVS -> [{ ns, k, t, v }]
      if (Array.isArray(formValues.items) && formValues.items.length > 0) {
        return formValues.items.map((it: any) => ({
          ns: String(it.ns || "cfg_eth").trim(),
          k: String(it.k || "").trim(),
          t: it.t || "str",
          v: validateAndFormatNvsValue(it.t || "str", it.v),
        }));
      }
      if (formValues.k !== undefined && formValues.v !== undefined) {
        return [
          {
            ns: String(formValues.ns || "cfg_eth").trim(),
            k: String(formValues.k || "").trim(),
            t: formValues.t || "str",
            v: validateAndFormatNvsValue(formValues.t || "str", formValues.v),
          },
        ];
      }
      return [];
    }

    case "objectArray": {
      // Метод 16, 20, 50, 51 и др.
      if (method.code === 16) {
        if (Array.isArray(formValues.items) && formValues.items.length > 0) {
          return formValues.items.map((it: any) => ({
            cd: String(it.cd || "").trim(),
            cl: Number(it.cl || 1),
          }));
        }
        if (formValues.cd) {
          return [
            {
              cd: String(formValues.cd).trim(),
              cl: Number(formValues.cl || 1),
            },
          ];
        }
      }

      if (method.code === 20) {
        return [{ mt: Number(formValues.mt ?? 4) }];
      }

      if (method.code === 50) {
        return [{ ns: String(formValues.ns || "cfg_eth").trim() }];
      }

      if (method.code === 51) {
        return [{ cl: Number(formValues.cl || 1) }];
      }

      // Общий случай для полей
      const dynamicObj: Record<string, any> = {};
      if (method.fields) {
        for (const field of method.fields) {
          if (formValues[field.name] !== undefined) {
            dynamicObj[field.name] =
              field.type === "number" ? Number(formValues[field.name]) : formValues[field.name];
          }
        }
      }
      return Object.keys(dynamicObj).length > 0 ? [dynamicObj] : [];
    }

    case "custom": {
      if (formValues.params_json && typeof formValues.params_json === "string" && formValues.params_json.trim()) {
        try {
          const parsed = JSON.parse(formValues.params_json.trim());
          if (Array.isArray(parsed)) {
            return parsed;
          }
          if (parsed && typeof parsed === "object" && Array.isArray(parsed.dt)) {
            return parsed.dt;
          }
          if (parsed && typeof parsed === "object") {
            return [parsed];
          }
        } catch {
          // Если не парсится, возвращаем как есть в объекте для отображения ошибки
          return [];
        }
      }
      return [];
    }

    default:
      return [];
  }
}

export interface BuildTaskInputOptions {
  deviceId: number;
  method: MethodDefinition;
  customMethodCode?: number;
  extTaskId?: string;
  priority?: number;
  ttl?: number;
  formValues: any;
}

/**
 * Сборка итогового JSON-пакета TaskCreate
 */
export function buildTaskCreatePayload(options: BuildTaskInputOptions): TaskCreatePayload {
  const { deviceId, method, customMethodCode, extTaskId, priority = 0, ttl = 60, formValues } = options;

  const actualMethodCode =
    method.code === CUSTOM_METHOD_CODE ? Number(customMethodCode || 0) : method.code;

  const dt = buildPayloadDt(method, formValues);

  return {
    ext_task_id: (extTaskId && extTaskId.trim()) || generateExtTaskId(),
    device_id: deviceId,
    method_code: actualMethodCode,
    priority: Math.min(Math.max(Number(priority || 0), 0), 9),
    ttl: Math.min(Math.max(Number(ttl || 60), 0), 44640),
    payload: {
      dt,
    },
  };
}
