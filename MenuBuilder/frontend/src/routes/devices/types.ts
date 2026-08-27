export interface MethodCatalogItem {
  code: number;
  label: string;
  description: string;
  defaultParams?: Record<string, any>;
  fields?: Array<{
    name: string;
    label: string;
    type: "string" | "number" | "boolean" | "json";
    defaultValue?: any;
    tooltip?: string;
  }>;
}

export const METHOD_CATALOG: MethodCatalogItem[] = [
  {
    code: 0,
    label: "0 - NOP (Без действия)",
    description: "Проверка связи и доступности канала RPC",
  },
  {
    code: 1,
    label: "1 - Пинг устройства",
    description: "Отправка echo-запроса на устройство",
  },
  {
    code: 2,
    label: "2 - Запрос статуса и телеметрии",
    description: "Получение текущих параметров и состояния оборудования",
  },
  {
    code: 20,
    label: "20 - Короткая команда",
    description: "Отправка короткой команды управления",
    fields: [
      {
        name: "dt_mt",
        label: "Код подкоманды",
        type: "number",
        defaultValue: 4,
        tooltip: "Числовой код подкоманды (например 4)",
      },
    ],
  },
  {
    code: 21,
    label: "21 - Перезагрузка устройства",
    description: "Штатная перезагрузка операционной системы устройства",
  },
  {
    code: 35,
    label: "35 - Ввод PIN-кода",
    description: "Удаленный ввод сервисного PIN-кода",
    fields: [
      {
        name: "pin",
        label: "PIN-код",
        type: "string",
        defaultValue: "",
        tooltip: "6-значный сервисный PIN-код",
      },
    ],
  },
  {
    code: 49,
    label: "49 - Запись параметра в БД устройства",
    description: "Запись настройки в локальное хранилище устройства",
    fields: [
      {
        name: "ns",
        label: "Раздел (Namespace)",
        type: "string",
        defaultValue: "system",
      },
      {
        name: "key",
        label: "Ключ (Key)",
        type: "string",
        defaultValue: "",
      },
      {
        name: "value",
        label: "Значение (Value)",
        type: "string",
        defaultValue: "",
      },
    ],
  },
  {
    code: 99,
    label: "99 - Произвольная команда (Custom JSON)",
    description: "Отправка произвольного JSON payload",
  },
];
