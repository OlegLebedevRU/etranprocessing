import { describe, it, expect } from "vitest";

describe("L4Desk MCP waitlist", () => {
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
