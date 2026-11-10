import { describe, expect, it } from "vitest";
import type { PaymentRecord } from "../api/reports";
import { paymentExport, reportCsv } from "../utils/paymentExport";

const item = (params: PaymentRecord["params"]): PaymentRecord => ({
  paym_id: 1, paym_ext_id: "0262_fixture", device_id: 262, sn: "SN", paym_tsp_code: 1,
  paym_amount: 12345, paym_datetime: "2026-11-10T12:00:00Z", paym_state: 2,
  paym_state_label: "Оплачен", pay_type_id: 1, pay_type_label: "Наличные", params,
});

describe("payment CSV", () => {
  it("unions all parameters, aligns absent values and keeps same-code descriptions distinct", () => {
    const { headers, rows } = paymentExport([
      item([{ code: 2, description: "ФИО", value: "Иванов" }, { code: 1, description: "Счёт", value: "00123" }]),
      item([{ code: 2, description: "Адрес", value: "Москва" }]),
    ], () => "10.11.2026");
    expect(headers.slice(9)).toEqual(["Параметр 1: Счёт", "Параметр 2: Адрес", "Параметр 2: ФИО"]);
    expect(rows[0].slice(9)).toEqual(["00123", "", "Иванов"]);
    expect(rows[1].slice(9)).toEqual(["", "Москва", ""]);
    expect(rows[0][5]).toBe("123.45");
  });
  it("preserves repeated values and escapes Unicode, delimiters and spreadsheet formulas", () => {
    const result = paymentExport([item([
      { code: 1, description: "Поле", value: "А;Б" }, { code: 1, description: "Поле", value: '"В"' },
      { code: 2, description: "Другое", value: "=1+2" },
    ])], v => v);
    expect(result.rows[0].slice(9)).toEqual(['А;Б\n"В"', "=1+2"]);
    const csv = reportCsv(result.headers, result.rows);
    expect(csv.startsWith("\uFEFF")).toBe(true);
    expect(csv).toContain('"А;Б\n""В"""');
    expect(csv).toContain("'=1+2");
  });
  it("exports fixed columns even when there are no parameters or rows", () => {
    expect(paymentExport([], v => v).headers).toHaveLength(9);
    expect(paymentExport([item([])], v => v).rows[0]).toHaveLength(9);
  });
});
