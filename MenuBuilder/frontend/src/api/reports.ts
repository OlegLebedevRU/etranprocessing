import axios from "./client";

export interface InkassRecord {
  id: number;
  device_id: number;
  sn: string;
  org_id: number;
  inkass_datetime: string;
  server_datetime: string;
  total_sum: number;
  total_count: number;
  total_note_sum: number;
  total_note_count: number;
  total_coin_sum: number;
  total_coin_count: number;
  notes: number[];
  coins: number[];
  inkassator: string;
  inkass_ext_id: string;
  paym_ext_id: string;
  inkass_id: string;
  cassette_num: string;
  cnt_inkass: number;
  cnt_inkass_sum: number;
  cnt_transact: number;
  cnt_total_sum: number;
  transact_count: number;
  last_sum_inkass: number;
  currency: number;
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
