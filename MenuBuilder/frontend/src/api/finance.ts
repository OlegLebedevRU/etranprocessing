import client from "./client";

export interface FinBalance {
  tenant_id: number;
  account_id: number;
  balance_kopecks: number;
  balance_rubles: number;
  version: number;
  last_transaction_id?: number | null;
  updated_at?: string | null;
}

export interface FinEntitlementStatus {
  tenant_id: number;
  entitlement: "free" | "active" | "grace" | "blocked" | string;
  balance_kopecks: number;
  is_first_paid: boolean;
  cycle_id?: number | null;
  cycle_starts_at?: string | null;
  cycle_ends_at?: string | null;
  grace_deadline?: string | null;
  can_start_sessions: boolean;
  free_terminal_id?: number | null;
  today_usage_seconds: number;
  free_quota_seconds: number;
  reason_code?: string | null;
  reason_message?: string | null;
}

export interface FinBillingProfile {
  tenant_id: number;
  billing_anchor_day: number;
  timezone: string;
  entitlement: string;
  current_cycle_id?: number | null;
  grace_deadline?: string | null;
  created_at?: string;
  updated_at?: string;
}

export interface FinBillingCycle {
  id: number;
  tenant_id: number;
  cycle_index: number;
  starts_at: string;
  ends_at: string;
  status: string;
  charged_amount_kopecks: number;
  created_at?: string;
}

export interface FinUsageDaily {
  id: number;
  tenant_id: number;
  terminal_id: number;
  local_date: string;
  session_count: number;
  console_seconds: number;
  video_seconds: number;
  total_seconds: number;
  free_seconds: number;
  chargeable_seconds: number;
  charged_amount_kopecks: number;
  status: string;
  posted_at?: string | null;
}

export interface FinTerminalMonthlyCharge {
  id: number;
  terminal_id: number;
  billing_cycle_id: number;
  amount_kopecks: number;
  status: string;
  posted_at?: string | null;
  first_online_at?: string | null;
}

export interface FinLedgerEntry {
  id: number;
  transaction_id: number;
  line_number: number;
  account_id: number;
  debit_kopecks: number;
  credit_kopecks: number;
}

export interface FinLedgerTransaction {
  id: number;
  tenant_id: number;
  operation_id: string;
  kind: string;
  status: string;
  debit_kopecks: number;
  credit_kopecks: number;
  source_project?: string | null;
  source_type?: string | null;
  calculation_snapshot?: Record<string, any> | null;
  actor?: string | null;
  correlation_id?: string | null;
  created_at: string;
  posted_at: string;
  entries?: FinLedgerEntry[];
}

export interface FinPayment {
  id: number;
  tenant_id: number;
  user_id?: number | null;
  provider_payment_id?: string | null;
  amount_rubles: number;
  amount_kopecks: number;
  status: string;
  confirmation_url?: string | null;
  created_at: string;
  paid_at?: string | null;
  correlation_id?: string | null;
}

export interface FinTariff {
  id: number;
  version_number: number;
  name: string;
  first_terminal_monthly_kopecks: number;
  additional_terminal_monthly_kopecks: number;
  usage_hourly_kopecks: number;
  free_daily_seconds: number;
  effective_from: string;
}

export async function getTenantBalance(): Promise<FinBalance> {
  const { data } = await client.get<FinBalance>("/v1/finance/balance");
  return data;
}

export async function getTenantEntitlement(): Promise<FinEntitlementStatus> {
  const { data } = await client.get<FinEntitlementStatus>("/v1/finance/entitlement");
  return data;
}

export async function getTenantBillingProfile(): Promise<FinBillingProfile> {
  const { data } = await client.get<FinBillingProfile>("/v1/finance/profile");
  return data;
}

export async function listTenantCycles(limit = 20): Promise<FinBillingCycle[]> {
  const { data } = await client.get<FinBillingCycle[]>("/v1/finance/cycles", {
    params: { limit },
  });
  return data;
}

export interface ListUsageParams {
  terminal_id?: number;
  start_date?: string;
  end_date?: string;
  limit?: number;
}

export async function listTenantDailyUsage(params?: ListUsageParams): Promise<FinUsageDaily[]> {
  const { data } = await client.get<FinUsageDaily[]>("/v1/finance/usage", {
    params,
  });
  return data;
}

export async function listTenantMonthlyCharges(
  billing_cycle_id?: number,
  limit = 50
): Promise<FinTerminalMonthlyCharge[]> {
  const { data } = await client.get<FinTerminalMonthlyCharge[]>("/v1/finance/monthly-charges", {
    params: { billing_cycle_id, limit },
  });
  return data;
}

export async function listTenantTransactions(limit = 50, offset = 0): Promise<FinLedgerTransaction[]> {
  const { data } = await client.get<FinLedgerTransaction[]>("/v1/finance/transactions", {
    params: { limit, offset },
  });
  return data;
}

export async function getCurrentTariff(): Promise<FinTariff> {
  const { data } = await client.get<FinTariff>("/v1/finance/tariffs/current");
  return data;
}

export interface CreatePaymentPayload {
  amount_rubles: number;
  return_url: string;
  idempotence_key?: string;
}

export async function createTopUpPayment(payload: CreatePaymentPayload): Promise<FinPayment> {
  const { data } = await client.post<FinPayment>("/v1/finance/payments", payload);
  return data;
}

export async function getPaymentStatus(paymentId: number): Promise<FinPayment> {
  const { data } = await client.get<FinPayment>(`/v1/finance/payments/${paymentId}`);
  return data;
}
