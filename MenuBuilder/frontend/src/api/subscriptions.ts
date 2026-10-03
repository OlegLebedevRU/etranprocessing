import client from "./client";

export interface Subscription {
  terminal_id: number;
  device_id?: number | null;
  name: string;
  state:
    | "free"
    | "active"
    | "grace"
    | "unpaid"
    | "expired"
    | "payments_disabled"
    | "admin_disabled"
    | "deleted";
  allowed: boolean;
  is_free: boolean;
  paid_until: string | null;
  grace_until: string | null;
  reason: string;
  action: string | null;
}
export interface SubscriptionSummary {
  payments_enabled: boolean;
  month_price_kopecks: number;
  can_create: boolean;
  items: Subscription[];
}
export interface OrderItem {
  terminal_id: number;
  months: number;
}
export interface Order {
  id: number;
  status: "pending" | "waiting_for_capture" | "succeeded" | "canceled";
  amount_kopecks: number;
  confirmation_url: string | null;
  items: OrderItem[];
  created_at: string;
}
export interface Quote {
  available: boolean;
  quote_hash: string;
  snapshot: { amount_kopecks: number; items: OrderItem[] };
  preview: { terminal_id: number; paid_until: string }[];
}
export interface UsageDay {
  terminal_id: number;
  date: string;
  video_seconds: number;
  console_seconds: number;
}

export async function getSubscriptions(org_id?: number) {
  return (
    await client.get<SubscriptionSummary>("/subscriptions", {
      params: { org_id },
    })
  ).data;
}
export async function getOrders() {
  return (await client.get<Order[]>("/subscriptions/orders")).data;
}
export async function getQuote(items: OrderItem[]) {
  return (await client.post<Quote>("/subscriptions/quote", { items })).data;
}
export async function createOrder(
  items: OrderItem[],
  quote_hash: string,
  operation_id: string,
) {
  return (
    await client.post<Order>("/subscriptions/orders", {
      items,
      quote_hash,
      operation_id,
    })
  ).data;
}
export async function checkOrder(id: number) {
  return (await client.post<Order>(`/subscriptions/orders/${id}/check`)).data;
}
export async function getUsage(params?: {
  start?: string;
  end?: string;
  terminal_id?: number;
}) {
  return (await client.get<UsageDay[]>("/usage", { params })).data;
}
export async function correctSubscription(
  terminal_id: number,
  body: {
    paid_until: string | null;
    expected_paid_until: string | null;
    reason: string;
    operation_id: string;
  },
) {
  return (
    await client.post(`/admin/subscriptions/${terminal_id}/correct`, body)
  ).data;
}
