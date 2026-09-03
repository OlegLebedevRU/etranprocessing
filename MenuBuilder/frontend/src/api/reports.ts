import axios from "./client";

export interface InkassBanknotes {
  n10: number;
  n50: number;
  n100: number;
  n200: number;
  n500: number;
  n1000: number;
  n2000: number;
  n5000: number;
}

export interface InkassRecord {
  id: number;
  device_id: number;
  sn: string;
  org_id: number;
  inkass_datetime: string;
  server_datetime: string;
  total_sum: number;
  calculated_sum: number | null;
  calc_status?: "matched" | "mismatch" | "needs_calc";
  calc_delta?: number | null;
  calc_strategy?: string | null;
  calc_lower_paym_ext_id?: string | null;
  calc_upper_paym_ext_id?: string | null;
  total_count: number;
  total_note_sum: number;
  total_note_count: number;
  total_coin_sum: number;
  total_coin_count: number;
  banknotes: InkassBanknotes;
  notes: number[];
  coins: number[];
  inkassator: string;
  inkass_ext_id: string;
  paym_ext_id: string;
  inkass_id: string;
  report_number: string;
  cassette_num: string;
  cnt_inkass: number;
  cnt_inkass_sum: number;
  cnt_transact: number;
  cnt_total_sum: number;
  transact_count: number;
  last_sum_inkass: number;
  currency: number;
}

export interface StrategyPreview {
  id: string;
  name: string;
  lower_bound: string | null;
  upper_bound: string | null;
  calculated_cash: number | null;
  delta: number | null;
  is_matched: boolean;
  description?: string;
}

export interface RecalculatePreviewResponse {
  record_id: number;
  device_id: number;
  report_number: string;
  fact_total_sum: number;
  current_status: "matched" | "mismatch" | "needs_calc";
  strategies: StrategyPreview[];
}

export interface ApplyCalculationResponse {
  status: string;
  calc_status: "matched" | "mismatch" | "needs_calc";
  calculated_sum: number | null;
  delta: number | null;
}

export async function getInkassRecalculatePreview(
  recordId: number
): Promise<RecalculatePreviewResponse> {
  const { data } = await axios.post(`/reports/inkass/${recordId}/recalculate-preview`);
  return data;
}

export async function applyInkassCalculation(
  recordId: number,
  strategyId: string
): Promise<ApplyCalculationResponse> {
  const { data } = await axios.post(`/reports/inkass/${recordId}/apply-calculation`, {
    strategy_id: strategyId,
  });
  return data;
}

export interface InkassResponse {
  items: InkassRecord[];
  total: number;
}

export async function getInkass(params: {
  date_from?: string;
  date_to?: string;
  device_ids?: number[];
  page?: number;
  size?: number;
}): Promise<InkassResponse> {
  const { data } = await axios.get("/reports/inkass", { params: {
    ...params,
    device_ids: params.device_ids?.join(","),
  }});
  return data;
}

// --- Payments report ---

export interface PaymentParam {
  code: number;
  description: string;
  value: string;
}

export interface PaymentRecord {
  paym_id: number;
  paym_datetime: string;
  paym_amount: number;
  paym_ext_id: string;
  paym_tsp_code: number;
  tsp_name?: string;
  menu_version?: number | null;
  paym_state: number;
  paym_state_label: string;
  pay_type_id: number;
  pay_type_label: string;
  device_id: number;
  sn: string;
  params: PaymentParam[];
}

export interface PaymentsResponse {
  items: PaymentRecord[];
  total: number;
}

export async function getPayments(params: {
  date_from?: string;
  date_to?: string;
  device_ids?: number[];
  tsp_code?: number;
  paym_state?: number;
  top?: number;
}): Promise<PaymentsResponse> {
  const { data } = await axios.get("/reports/payments", {
    params: {
      ...params,
      device_ids: params.device_ids?.join(","),
    },
  });
  return data;
}

// --- Balance by terminal ---

export interface BalanceDayData {
  amount: number;
  count: number;
}

export interface BalanceByTerminalRecord {
  device_id: number;
  sn: string;
  terminal_id: number;
  tsp_count: number;
  total_count: number;
  total_amount: number;
  days?: Record<string, BalanceDayData>;
}

export async function getBalanceByTerminal(params: {
  date_from?: string;
  date_to?: string;
  device_ids?: number[];
  tsp_code?: number;
}): Promise<{ items: BalanceByTerminalRecord[] }> {
  const { data } = await axios.get("/reports/balance-by-terminal", {
    params: {
      ...params,
      device_ids: params.device_ids?.join(","),
    },
  });
  return data;
}

// --- Balance by TSP ---

export interface BalanceByTspRecord {
  tsp_code: number;
  version?: number | null;
  tsp_name: string;
  terminal_count: number;
  total_count: number;
  total_amount: number;
}

export async function getBalanceByTsp(params: {
  date_from?: string;
  date_to?: string;
  device_ids?: number[];
}): Promise<{ items: BalanceByTspRecord[] }> {
  const { data } = await axios.get("/reports/balance-by-tsp", {
    params: {
      ...params,
      device_ids: params.device_ids?.join(","),
    },
  });
  return data;
}
