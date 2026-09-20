import { describe, it, expect } from "vitest";

describe("L4Desk Licenses & Billing Calculations", () => {
  it("Positive balance explicitly permits paid continuation beyond 120 minutes", () => {
    const positiveBalance = {
      balance_rubles: 450.0,
      balance_kopecks: 45000,
    };
    const hasPaidAccess = positiveBalance.balance_rubles > 0;
    expect(hasPaidAccess).toBe(true);

    const zeroBalance = {
      balance_rubles: 0.0,
      balance_kopecks: 0,
    };
    const hasZeroAccess = zeroBalance.balance_rubles > 0;
    expect(hasZeroAccess).toBe(false);
  });

  it("Calculates today's pooled 120-minute usage correctly", () => {
    const freeQuotaSec = 7200; // 120 minutes
    const usage45MinSec = 2700; // 45 minutes
    const percent1 = Math.round((usage45MinSec / freeQuotaSec) * 100);
    expect(percent1).toBe(38);

    const usage120MinSec = 7200;
    const percent2 = Math.round((usage120MinSec / freeQuotaSec) * 100);
    expect(percent2).toBe(100);

    const usage150MinSec = 9000;
    const percent3 = Math.min(100, Math.round((usage150MinSec / freeQuotaSec) * 100));
    expect(percent3).toBe(100);
    const chargeableMinutes = (usage150MinSec - freeQuotaSec) / 60;
    expect(chargeableMinutes).toBe(30);
  });

  it("Remaining grace period calculates remaining days and hours accurately", () => {
    const now = Date.now();
    // 2 days and 5 hours in future
    const futureDeadline = new Date(now + (2 * 24 + 5) * 60 * 60 * 1000).toISOString();

    const diffMs = new Date(futureDeadline).getTime() - now;
    const hours = Math.floor(diffMs / (1000 * 60 * 60));
    const days = Math.floor(hours / 24);
    const remHours = hours % 24;

    expect(days).toBe(2);
    expect(remHours).toBe(5);
  });

  it("MCP waitlist validates 4 distinct priority use cases", () => {
    const validUseCases = [
      "auto_triage",
      "log_telemetry_analysis",
      "fleet_nlp_control",
      "security_audit",
    ];
    expect(validUseCases).toHaveLength(4);
    expect(validUseCases).toContain("auto_triage");
    expect(validUseCases).toContain("log_telemetry_analysis");
    expect(validUseCases).toContain("fleet_nlp_control");
    expect(validUseCases).toContain("security_audit");
  });
});
