import type { PaymentRecord } from "../api/reports";

/** Include every parameter present in the displayed result, keeping code/name distinct. */
export function paymentExport(items: PaymentRecord[], formatTime: (value: string) => string) {
  const key = (code: number, description: string) => JSON.stringify([code, description]);
  const parameters = new Map<string, { code: number; description: string }>();
  for (const item of items) for (const param of item.params) {
    parameters.set(key(param.code, param.description), param);
  }
  const columns = [...parameters.values()].sort((a, b) => a.code - b.code || a.description.localeCompare(b.description, "ru"));
  const headers = [
    "Транзакция", "Терминал", "Серийный номер", "Код ТСП", "ТСП",
    "Сумма (руб)", "Время", "Статус", "Тип",
    ...columns.map(p => `Параметр ${p.code}${p.description ? `: ${p.description}` : ""}`),
  ];
  const rows = items.map(item => [
    item.paym_ext_id, item.device_id, item.sn || "", item.paym_tsp_code || "", item.tsp_name || "",
    (item.paym_amount / 100).toFixed(2), formatTime(item.paym_datetime), item.paym_state_label, item.pay_type_label,
    ...columns.map(column => item.params
      .filter(p => p.code === column.code && p.description === column.description)
      .map(p => p.value).join("\n")),
  ]);
  return { headers, rows };
}

export function reportCsv(headers: string[], rows: (string | number)[][]): string {
  const cell = (value: string | number) => {
    let text = String(value ?? "");
    // User-supplied payment parameters must remain text in spreadsheet software.
    if (/^[\s]*[=+@-]/.test(text) && !/^-?\d+(?:[.,]\d+)?$/.test(text)) text = `'${text}`;
    return /[;"\r\n]/.test(text) ? `"${text.replace(/"/g, '""')}"` : text;
  };
  return "\uFEFF" + [headers, ...rows].map(row => row.map(cell).join(";")).join("\r\n");
}
