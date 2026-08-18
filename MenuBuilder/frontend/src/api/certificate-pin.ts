import client from "./client";

/** Terminal certificate PIN is ready to be entered manually on the terminal. */
export interface PinReadyResponse {
  status: "pin_ready";
  payment_required: false;
  terminal_id: number;
  pin: string;
  expires_at: string;
}

/** A billing order was created for the PIN and must be paid before it is issued. */
export interface PaymentRequiredResponse {
  status: "payment_required";
  payment_required: true;
  terminal_id: number;
  order_id: string;
  amount_minor: number;
  currency: string;
  payment_url: string | null;
}

export type CertificatePinResponse = PinReadyResponse | PaymentRequiredResponse;

/**
 * Request permission to create a certificate PIN for a terminal.
 *
 * Idempotent: calling this again while a pending PIN or pending order exists
 * returns the same PIN/order instead of creating a new one. This is also how
 * the frontend retrieves the PIN after a payment is confirmed — call this
 * endpoint again once the order is paid.
 */
export async function requestCertificatePin(
  terminalId: number,
): Promise<CertificatePinResponse> {
  const res = await client.post(`/billing/terminals/${terminalId}/certificate-pin`);
  return res.data;
}
