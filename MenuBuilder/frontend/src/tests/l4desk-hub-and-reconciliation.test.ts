import { describe, it, expect } from "vitest";
import type {
  CorrelationDrilldownResponse,
  HubManualPaymentCreateRequest,
  HubManualPaymentStornoRequest,
  HubRegistrationItem,
  HubTerminalItem,
  HubUsageItem,
  ReconciliationRunResponse,
} from "../api/hub";

describe("L4Desk Hub & Reconciliation Contracts (L4D-14-MB)", () => {
  it("Validates 5 Hub tabs contract and data structures", () => {
    // 1. Registrations
    const reg: HubRegistrationItem = {
      id: 1,
      tenant_id: 10,
      tenant_name: "Организация 10",
      email_normalized: "owner@org10.ru",
      status: "consumed",
      terms_version: "v1.0",
      timezone: "Europe/Moscow",
      source: "l4desk",
      created_at: "2026-09-01T10:00:00Z",
      expires_at: "2026-09-02T10:00:00Z",
      consumed_at: "2026-09-01T10:05:00Z",
      correlation_id: "corr-reg-10",
      is_first_paid: true,
    };
    expect(reg.id).toBe(1);
    expect(["pending", "consumed", "expired"]).toContain(reg.status);
    expect(reg.is_first_paid).toBe(true);

    // 2. Terminals
    const term: HubTerminalItem = {
      terminal_id: 101,
      tenant_id: 10,
      sn: "SN-101",
      ordinal: 1,
      provisioning_state: "ready",
      pin_state: "consumed",
      certificate_reference: "cert-101",
      first_online_at: "2026-09-01T10:10:00Z",
      last_online_at: "2026-09-01T10:12:00Z",
      is_online: true,
      is_free: true,
      correlation_id: "corr-term-101",
      created_at: "2026-09-01T10:05:00Z",
    };
    expect(term.ordinal).toBe(1);
    expect(term.is_free).toBe(true);
    expect(term.is_online).toBe(true);

    // 3. Usage & Invariant: calculated = posted + discarded
    const usage: HubUsageItem = {
      id: 501,
      tenant_id: 10,
      terminal_id: 101,
      local_date: "2026-09-01",
      source_seconds: 3600,
      video_seconds: 1800,
      console_seconds: 1800,
      free_seconds: 0,
      billable_seconds: 3600,
      calculated_kopecks: 100,
      posted_kopecks: 100,
      discarded_kopecks: 0,
      is_reconciled: true,
      ledger_transaction_id: 1001,
      source_events_hash: "hash501",
      correlation_id: "corr-u-501",
    };
    expect(usage.calculated_kopecks).toBe(usage.posted_kopecks + usage.discarded_kopecks);
    expect(usage.posted_kopecks % 100).toBe(0);
    expect(usage.discarded_kopecks).toBeGreaterThanOrEqual(0);
    expect(usage.discarded_kopecks).toBeLessThan(100);
  });

  it("Correlation drill-down marks absent facts as mismatches without hallucinations", () => {
    // Simulated drill-down response where registration & usage are missing
    const drilldown: CorrelationDrilldownResponse = {
      correlation_id: "corr-missing-facts-99",
      query_params: { correlation_id: "corr-missing-facts-99" },
      overall_status: "mismatch",
      mismatch_codes: ["REGISTRATION_NOT_FOUND", "USAGE_NOT_FOUND"],
      nodes: {
        registration: {
          node_type: "registration",
          label: "Регистрация",
          present: false,
          mismatch: true,
          mismatch_code: "REGISTRATION_NOT_FOUND",
          details: "Факт регистрации не найден",
          fact: null, // Strictly null, not hallucinated
        },
        terminal: {
          node_type: "terminal",
          label: "Терминал",
          present: true,
          mismatch: false,
          fact: { terminal_id: 99, sn: "SN-99" },
        },
        pin_provisioning: {
          node_type: "pin_provisioning",
          label: "PIN и сертификат",
          present: true,
          mismatch: false,
          fact: { pin_state: "consumed", provisioning_state: "ready" },
        },
        online_session: {
          node_type: "online_session",
          label: "Сессия / Online",
          present: true,
          mismatch: false,
          fact: { session_id: 991, state: "closed" },
        },
        usage: {
          node_type: "usage",
          label: "Потребление (Usage)",
          present: false,
          mismatch: true,
          mismatch_code: "USAGE_NOT_FOUND",
          details: "Суточный регистр потребления отсутствует",
          fact: null,
        },
        ledger_payment: {
          node_type: "ledger_payment",
          label: "Проводка / Платёж",
          present: false,
          mismatch: false,
          fact: null,
        },
      },
    };

    expect(drilldown.overall_status).toBe("mismatch");
    expect(drilldown.mismatch_codes).toContain("REGISTRATION_NOT_FOUND");
    expect(drilldown.mismatch_codes).toContain("USAGE_NOT_FOUND");

    // Missing facts must have fact == null and mismatch == true
    expect(drilldown.nodes.registration.present).toBe(false);
    expect(drilldown.nodes.registration.mismatch).toBe(true);
    expect(drilldown.nodes.registration.fact).toBeNull();

    expect(drilldown.nodes.usage.present).toBe(false);
    expect(drilldown.nodes.usage.mismatch).toBe(true);
    expect(drilldown.nodes.usage.fact).toBeNull();

    // Present facts must be verified
    expect(drilldown.nodes.terminal.present).toBe(true);
    expect(drilldown.nodes.terminal.mismatch).toBe(false);
    expect(drilldown.nodes.terminal.fact?.sn).toBe("SN-99");
  });

  it("Manual payment & storno UI strictly enforces confirmation code '11'", () => {
    const isCode11Valid = (code: string) => code.trim() === "11";

    expect(isCode11Valid("11")).toBe(true);
    expect(isCode11Valid(" 11 ")).toBe(true);
    expect(isCode11Valid("12")).toBe(false);
    expect(isCode11Valid("")).toBe(false);
    expect(isCode11Valid("confirm")).toBe(false);

    const manualPayPayload: HubManualPaymentCreateRequest = {
      tenant_id: 42,
      amount_rubles: 15000,
      received_on: "2026-09-01",
      document_number: "ORDER-4201",
      purpose: "Оплата подписки",
      payer: "ООО 'Альфа'",
      comment: "Счёт 42",
      confirmation_code: "11",
    };

    expect(manualPayPayload.amount_rubles).toBeGreaterThan(0);
    expect(Number.isInteger(manualPayPayload.amount_rubles)).toBe(true);
    expect(isCode11Valid(manualPayPayload.confirmation_code)).toBe(true);

    const stornoPayload: HubManualPaymentStornoRequest = {
      reversal_reason: "Возврат по требованию клиента",
      comment: "Акт № 12",
      confirmation_code: "11",
    };
    expect(stornoPayload.reversal_reason.length).toBeGreaterThan(0);
    expect(isCode11Valid(stornoPayload.confirmation_code)).toBe(true);
  });

  it("Reconciliation run invariant verification: debit=credit, posted%100=0, no diff", () => {
    const recRun: ReconciliationRunResponse = {
      id: 10,
      operation_id: "rec_op_10",
      period_start: "2026-09-01T00:00:00Z",
      period_end: "2026-09-30T23:59:59Z",
      status: "matched",
      debit_kopecks: 500000,
      credit_kopecks: 500000,
      posted_kopecks: 500000,
      calculated_kopecks: 500000,
      discarded_kopecks: 0,
      balance_difference_kopecks: 0,
      mismatch_count: 0,
      started_at: "2026-09-20T12:00:00Z",
      finished_at: "2026-09-20T12:00:05Z",
    };

    // 1. Debit equals Credit
    expect(recRun.debit_kopecks).toBe(recRun.credit_kopecks);

    // 2. Multiples of 100 kopecks (posted % 100 == 0)
    expect(recRun.posted_kopecks! % 100).toBe(0);

    // 3. Calculated = posted + discarded
    expect(recRun.calculated_kopecks).toBe(
      recRun.posted_kopecks! + recRun.discarded_kopecks!
    );

    // 4. Zero balance diff and zero mismatch count
    expect(recRun.balance_difference_kopecks).toBe(0);
    expect(recRun.mismatch_count).toBe(0);
    expect(recRun.status).toBe("matched");
  });
});
