import client from "./client";

export interface BillingForecastMonth {
  month: string;
  amount_minor: number;
  terminal_count: number;
}

export interface BillingSummary {
  as_of: string;
  currency: string;
  monthly_base_price_minor: number;
  overdue_amount_minor: number;
  overdue_terminal_count: number;
  active_terminal_count: number;
  deactivation_scheduled_count: number;
  disabled_terminal_count: number;
  admin_disabled_terminal_count: number;
  nearest_required_payment_at: string | null;
  forecast: BillingForecastMonth[];
}

export interface BillingTerminal {
  terminal_id: number;
  device_id: number;
  sn: string;
  terminal_is_active: boolean;
  license_id: number | null;
  license_expires_at: string | null;
  renewal_enabled: boolean;
  deactivation_requested_at: string | null;
  billing_status: string;
  monthly_price_minor: number;
  billing_period_months: number;
  period_price_minor: number;
  periods_due: number;
  overdue_amount_minor: number;
  next_payment_at: string | null;
  next_payment_amount_minor: number;
  projected_expires_at_after_debt_payment: string | null;
  can_deactivate: boolean;
  can_cancel_deactivation: boolean;
  can_reactivate: boolean;
  included_in_forecast: boolean;
  cert_not_valid_after: string | null;
  tenant_pin_creation_enabled: boolean;
}

export interface DeactivateResponse {
  terminal_id: number;
  status: string;
  works_until: string | null;
  overdue_amount_minor: number;
  included_in_forecast: boolean;
}

export interface CancelDeactivationResponse {
  terminal_id: number;
  status: string;
  renewal_enabled: boolean;
}

export interface CheckoutItemRequest {
  terminal_id: number;
  advance_periods: number;
}

export interface CheckoutRequest {
  items: CheckoutItemRequest[];
}

export interface CheckoutItemResponse {
  terminal_id: number;
  periods_due: number;
  advance_periods: number;
  amount_minor: number;
  new_expires_at: string;
}

export interface CheckoutResponse {
  order_id: string;
  currency: string;
  amount_minor: number;
  payment_url: string | null;
  items: CheckoutItemResponse[];
}

export interface ReactivationCheckoutRequest {
  advance_periods: number;
}

export interface ReactivationCheckoutResponse {
  order_id: string;
  currency: string;
  amount_minor: number;
  months: number;
  new_expires_at: string;
  payment_url: string | null;
}

export async function getBillingSummary(): Promise<BillingSummary> {
  const res = await client.get("/billing/summary");
  return res.data;
}

export async function getBillingTerminals(
  status?: string,
  search?: string,
): Promise<BillingTerminal[]> {
  const params: Record<string, string> = {};
  if (status) params.status = status;
  if (search) params.search = search;
  const res = await client.get("/billing/terminals", { params });
  return res.data;
}

export async function deactivateTerminal(
  terminalId: number,
): Promise<DeactivateResponse> {
  const res = await client.post(`/billing/terminals/${terminalId}/deactivate`);
  return res.data;
}

export async function cancelDeactivation(
  terminalId: number,
): Promise<CancelDeactivationResponse> {
  const res = await client.post(
    `/billing/terminals/${terminalId}/cancel-deactivation`,
  );
  return res.data;
}

export async function createCheckout(
  data: CheckoutRequest,
): Promise<CheckoutResponse> {
  const res = await client.post("/billing/checkout", data);
  return res.data;
}

export async function createReactivationCheckout(
  terminalId: number,
  data: ReactivationCheckoutRequest,
): Promise<ReactivationCheckoutResponse> {
  const res = await client.post(
    `/billing/terminals/${terminalId}/reactivation-checkout`,
    data,
  );
  return res.data;
}

export interface ConfirmPaymentResponse {
  order_id: string;
  status: string;
  paid_at: string | null;
  items_updated: number;
}

export async function confirmPayment(
  orderId: string,
): Promise<ConfirmPaymentResponse> {
  const res = await client.post(`/billing/orders/${orderId}/confirm`);
  return res.data;
}
