import client from "./client";

export interface HubRegistrationItem {
  id: number;
  tenant_id: number | null;
  tenant_name?: string | null;
  email_normalized: string;
  status: "pending" | "consumed" | "expired";
  terms_version: string;
  timezone: string;
  source?: string | null;
  created_at: string;
  expires_at: string;
  consumed_at?: string | null;
  correlation_id: string;
  is_first_paid: boolean;
}

export interface HubRegistrationsResponse {
  items: HubRegistrationItem[];
  total: number;
  page: number;
  page_size: number;
}

export interface HubTerminalItem {
  terminal_id: number;
  tenant_id: number;
  tenant_name?: string | null;
  sn: string;
  ordinal: number;
  external_terminal_id?: string | null;
  provisioning_state: string;
  pin_state: string;
  certificate_reference?: string | null;
  first_online_at?: string | null;
  last_online_at?: string | null;
  is_online: boolean;
  is_free: boolean;
  last_error?: string | null;
  correlation_id: string;
  created_at: string;
}

export interface HubTerminalsResponse {
  items: HubTerminalItem[];
  total: number;
  page: number;
  page_size: number;
}

export interface HubSessionItem {
  id: number;
  tenant_id: number;
  terminal_id: number;
  terminal_sn?: string | null;
  operation_id: string;
  correlation_id: string;
  session_type: "console" | "video";
  state: string;
  requested_at: string;
  active_at?: string | null;
  closed_at?: string | null;
  duration_seconds: number;
  reason?: string | null;
  source_events_hash?: string | null;
}

export interface HubSessionsResponse {
  items: HubSessionItem[];
  total: number;
  page: number;
  page_size: number;
}

export interface HubUsageItem {
  id: number;
  tenant_id: number;
  terminal_id: number;
  terminal_sn?: string | null;
  local_date: string;
  source_seconds: number;
  video_seconds: number;
  console_seconds: number;
  free_seconds: number;
  billable_seconds: number;
  calculated_kopecks: number;
  posted_kopecks: number;
  discarded_kopecks: number;
  is_reconciled: boolean;
  ledger_transaction_id?: number | null;
  source_events_hash: string;
  correlation_id: string;
  posted_at?: string | null;
}

export interface HubUsageResponse {
  items: HubUsageItem[];
  total: number;
  page: number;
  page_size: number;
}

export interface HubTenantFinanceItem {
  tenant_id: number;
  tenant_name?: string | null;
  balance_kopecks: number;
  balance_rubles: number;
  entitlement: "active" | "grace" | "blocked";
  anchor_day?: number | null;
  current_cycle_ends_at?: string | null;
  grace_deadline?: string | null;
  terminal_count: number;
  has_failed_notifications: boolean;
}

export interface HubFinanceOverviewResponse {
  total_tenants: number;
  active_tenants: number;
  grace_tenants: number;
  blocked_tenants: number;
  total_balance_rubles: number;
  tenants: HubTenantFinanceItem[];
  total: number;
  page: number;
  page_size: number;
}

export interface HubPaymentItem {
  id: number;
  tenant_id: number;
  tenant_name?: string | null;
  source: "yookassa" | "manual" | "storno";
  amount_rubles: number;
  amount_kopecks: number;
  status: string;
  reference: string;
  details?: string | null;
  payer?: string | null;
  purpose?: string | null;
  comment?: string | null;
  ledger_transaction_id?: number | null;
  created_at: string;
  correlation_id: string;
  storno_by_id?: number | null;
}

export interface HubPaymentsResponse {
  items: HubPaymentItem[];
  total: number;
  page: number;
  page_size: number;
}

export interface HubNotificationItem {
  id: number;
  tenant_id: number;
  tenant_name?: string | null;
  billing_cycle_id: number;
  notification_type: string;
  scheduled_at: string;
  status: string;
  attempts: number;
  sent_at?: string | null;
  last_error?: string | null;
  correlation_id: string;
}

export interface HubNotificationsResponse {
  items: HubNotificationItem[];
  total: number;
  page: number;
  page_size: number;
}

export interface HubAuditEventItem {
  id: number;
  tenant_id?: number | null;
  actor: string;
  event_type: string;
  subject_type: string;
  subject_id?: string | null;
  outcome: string;
  details?: Record<string, unknown> | null;
  correlation_id: string;
  occurred_at: string;
}

export interface HubAuditEventsResponse {
  items: HubAuditEventItem[];
  total: number;
  page: number;
  page_size: number;
}

export interface CorrelationFactNode {
  node_type: string;
  label: string;
  present: boolean;
  mismatch: boolean;
  mismatch_code?: string | null;
  details?: string | null;
  fact?: Record<string, unknown> | null;
}

export interface CorrelationDrilldownResponse {
  correlation_id?: string | null;
  query_params: Record<string, unknown>;
  overall_status: "matched" | "mismatch";
  mismatch_codes: string[];
  nodes: Record<string, CorrelationFactNode>;
}

export interface HubManualPaymentCreateRequest {
  tenant_id: number;
  amount_rubles: number;
  received_on: string;
  document_number: string;
  purpose: string;
  payer: string;
  comment?: string;
  evidence_reference?: string;
  confirmation_code: string; // must be "11"
  correlation_id?: string;
}

export interface HubManualPaymentStornoRequest {
  reversal_reason: string;
  comment?: string;
  confirmation_code: string; // must be "11"
  correlation_id?: string;
}

export interface ReconciliationRunResponse {
  id: number;
  operation_id: string;
  period_start: string;
  period_end: string;
  tenant_id?: number | null;
  status: "matched" | "mismatch";
  calculated_kopecks?: number | null;
  posted_kopecks?: number | null;
  discarded_kopecks?: number | null;
  debit_kopecks?: number | null;
  credit_kopecks?: number | null;
  balance_difference_kopecks?: number | null;
  mismatch_count?: number | null;
  details?: Record<string, unknown> | null;
  started_at: string;
  finished_at?: string | null;
}

// API Methods
export async function fetchHubRegistrations(
  params?: Record<string, unknown>
): Promise<HubRegistrationsResponse> {
  const { data } = await client.get<HubRegistrationsResponse>("/v1/admin/hub/registrations", {
    params,
  });
  return data;
}

export async function fetchHubTerminals(
  params?: Record<string, unknown>
): Promise<HubTerminalsResponse> {
  const { data } = await client.get<HubTerminalsResponse>("/v1/admin/hub/terminals", {
    params,
  });
  return data;
}

export async function fetchHubSessions(
  params?: Record<string, unknown>
): Promise<HubSessionsResponse> {
  const { data } = await client.get<HubSessionsResponse>("/v1/admin/hub/sessions", {
    params,
  });
  return data;
}

export async function fetchHubUsage(
  params?: Record<string, unknown>
): Promise<HubUsageResponse> {
  const { data } = await client.get<HubUsageResponse>("/v1/admin/hub/usage", {
    params,
  });
  return data;
}

export async function fetchHubFinanceOverview(
  params?: Record<string, unknown>
): Promise<HubFinanceOverviewResponse> {
  const { data } = await client.get<HubFinanceOverviewResponse>(
    "/v1/admin/hub/finance/overview",
    { params }
  );
  return data;
}

export async function fetchHubPayments(
  params?: Record<string, unknown>
): Promise<HubPaymentsResponse> {
  const { data } = await client.get<HubPaymentsResponse>("/v1/admin/hub/finance/payments", {
    params,
  });
  return data;
}

export async function fetchHubNotifications(
  params?: Record<string, unknown>
): Promise<HubNotificationsResponse> {
  const { data } = await client.get<HubNotificationsResponse>("/v1/admin/hub/notifications", {
    params,
  });
  return data;
}

export async function fetchHubAuditEvents(
  params?: Record<string, unknown>
): Promise<HubAuditEventsResponse> {
  const { data } = await client.get<HubAuditEventsResponse>("/v1/admin/hub/audit-events", {
    params,
  });
  return data;
}

export async function fetchHubCorrelationDrilldown(
  params: Record<string, unknown>
): Promise<CorrelationDrilldownResponse> {
  const { data } = await client.get<CorrelationDrilldownResponse>(
    "/v1/admin/hub/correlation-drilldown",
    { params }
  );
  return data;
}

export async function createHubManualPayment(
  body: HubManualPaymentCreateRequest
): Promise<unknown> {
  const { data } = await client.post("/v1/admin/hub/finance/manual-payment", body);
  return data;
}

export async function stornoHubManualPayment(
  manualPaymentId: number,
  body: HubManualPaymentStornoRequest
): Promise<unknown> {
  const { data } = await client.post(
    `/v1/admin/hub/finance/manual-payment/${manualPaymentId}/storno`,
    body
  );
  return data;
}

export async function triggerHubReconciliation(
  body: Record<string, unknown>
): Promise<ReconciliationRunResponse> {
  const { data } = await client.post<ReconciliationRunResponse>(
    "/v1/admin/hub/reconciliation/run",
    body
  );
  return data;
}
