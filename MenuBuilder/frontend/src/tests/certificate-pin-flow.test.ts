import { describe, it, expect, vi, beforeEach } from "vitest";

interface TerminalInput {
  terminal_id: number;
  device_id: number;
  cert_pin_pending?: boolean;
}

interface PinFlowState {
  step: "checking" | "payment_required" | "paying" | "pin_ready" | "error";
  result: any;
  hasNotified: boolean;
}

/**
 * Controller modeling the CertificatePinModal state machine and guard logic.
 */
class CertificatePinFlowController {
  public state: PinFlowState = {
    step: "checking",
    result: null,
    hasNotified: false,
  };

  private lastLoadedTerminalId: number | null = null;
  private onIssuedCb: (() => void) | undefined;

  constructor(
    private readonly requestPinApi: (id: number) => Promise<any>,
    private readonly confirmPaymentApi: (orderId: string) => Promise<any>,
  ) {}

  public updateOnIssued(cb?: () => void) {
    this.onIssuedCb = cb;
  }

  public async onModalOpen(open: boolean, terminal: TerminalInput | null) {
    if (!open || !terminal) {
      this.lastLoadedTerminalId = null;
      return;
    }

    // Guard: Only load once per modal session / terminalId
    if (this.lastLoadedTerminalId === terminal.terminal_id) {
      return;
    }

    this.lastLoadedTerminalId = terminal.terminal_id;
    this.state.hasNotified = false;
    await this.load(terminal);
  }

  public async load(terminal: TerminalInput) {
    this.state.step = "checking";
    try {
      const res = await this.requestPinApi(terminal.terminal_id);
      this.state.result = res;
      this.state.step = res.status === "pin_ready" ? "pin_ready" : "payment_required";

      // Guard: Only notify on new issuance, never for already pending PINs, and at most once
      if (res.status === "pin_ready" && !terminal.cert_pin_pending && !this.state.hasNotified) {
        this.state.hasNotified = true;
        this.onIssuedCb?.();
      }
    } catch {
      this.state.step = "error";
    }
  }

  public async handlePay() {
    if (this.state.result?.status !== "payment_required") return;
    this.state.step = "paying";
    try {
      await this.confirmPaymentApi(this.state.result.order_id);
      const res = await this.requestPinApi(this.state.result.terminal_id);
      this.state.result = res;
      if (res.status === "pin_ready") {
        this.state.step = "pin_ready";
        if (!this.state.hasNotified) {
          this.state.hasNotified = true;
          this.onIssuedCb?.();
        }
      } else {
        this.state.step = "payment_required";
      }
    } catch {
      this.state.step = "error";
    }
  }
}

describe("Certificate PIN Flow & Infinite Loop Prevention", () => {
  let mockRequestPin: any;
  let mockConfirmPayment: any;
  let mockOnIssued: any;

  beforeEach(() => {
    mockRequestPin = vi.fn();
    mockConfirmPayment = vi.fn().mockResolvedValue({});
    mockOnIssued = vi.fn();
  });

  it("Opening modal for already pending PIN (viewing) does NOT notify onIssued or loop", async () => {
    mockRequestPin.mockResolvedValue({
      status: "pin_ready",
      terminal_id: 1319,
      pin: "987654",
      expires_at: "2026-09-18T12:00:00Z",
    });

    const controller = new CertificatePinFlowController(mockRequestPin, mockConfirmPayment);
    controller.updateOnIssued(mockOnIssued);

    const terminal: TerminalInput = {
      terminal_id: 1319,
      device_id: 101,
      cert_pin_pending: true,
    };

    // Open modal
    await controller.onModalOpen(true, terminal);

    expect(mockRequestPin).toHaveBeenCalledTimes(1);
    expect(mockRequestPin).toHaveBeenCalledWith(1319);
    expect(controller.state.step).toBe("pin_ready");
    expect(controller.state.result.pin).toBe("987654");

    // CRITICAL: onIssued must NOT be called when viewing an already pending PIN
    expect(mockOnIssued).not.toHaveBeenCalled();

    // Simulate parent component re-renders (passing brand new arrow function references to onIssued)
    for (let i = 0; i < 5; i++) {
      const newCallback = vi.fn();
      controller.updateOnIssued(newCallback);
      await controller.onModalOpen(true, terminal);
    }

    // Verify no repeated API calls or onIssued invocations occur
    expect(mockRequestPin).toHaveBeenCalledTimes(1);
    expect(mockOnIssued).not.toHaveBeenCalled();
  });

  it("Opening modal for free PIN (not yet pending) issues PIN and calls onIssued exactly once", async () => {
    mockRequestPin.mockResolvedValue({
      status: "pin_ready",
      terminal_id: 1320,
      pin: "123456",
      expires_at: "2026-09-18T12:00:00Z",
    });

    const controller = new CertificatePinFlowController(mockRequestPin, mockConfirmPayment);
    controller.updateOnIssued(mockOnIssued);

    const terminal: TerminalInput = {
      terminal_id: 1320,
      device_id: 102,
      cert_pin_pending: false,
    };

    // Open modal
    await controller.onModalOpen(true, terminal);

    expect(mockRequestPin).toHaveBeenCalledTimes(1);
    expect(mockRequestPin).toHaveBeenCalledWith(1320);
    expect(controller.state.step).toBe("pin_ready");
    expect(mockOnIssued).toHaveBeenCalledTimes(1);

    // Parent re-renders
    controller.updateOnIssued(vi.fn());
    await controller.onModalOpen(true, terminal);

    // Stays at 1 call — no loop!
    expect(mockRequestPin).toHaveBeenCalledTimes(1);
  });

  it("Paid PIN flow calls onIssued exactly once after payment confirmation", async () => {
    mockRequestPin
      .mockResolvedValueOnce({
        status: "payment_required",
        terminal_id: 1321,
        order_id: "order-uuid-1",
        amount_minor: 50000,
        currency: "RUB",
      })
      .mockResolvedValueOnce({
        status: "pin_ready",
        terminal_id: 1321,
        pin: "555666",
        expires_at: "2026-09-18T12:00:00Z",
      });

    const controller = new CertificatePinFlowController(mockRequestPin, mockConfirmPayment);
    controller.updateOnIssued(mockOnIssued);

    const terminal: TerminalInput = {
      terminal_id: 1321,
      device_id: 103,
      cert_pin_pending: false,
    };

    // Open modal
    await controller.onModalOpen(true, terminal);
    expect(controller.state.step).toBe("payment_required");
    expect(mockOnIssued).not.toHaveBeenCalled();

    // User pays
    await controller.handlePay();

    expect(mockConfirmPayment).toHaveBeenCalledWith("order-uuid-1");
    expect(mockRequestPin).toHaveBeenCalledTimes(2);
    expect(controller.state.step).toBe("pin_ready");
    expect(mockOnIssued).toHaveBeenCalledTimes(1);

    // Simulate parent re-render
    controller.updateOnIssued(vi.fn());
    await controller.onModalOpen(true, terminal);
    expect(mockRequestPin).toHaveBeenCalledTimes(2);
  });
});
