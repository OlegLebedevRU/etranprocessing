/**
 * Session Lifecycle and Generation Coordinator for MenuBuilder Video Surveillance.
 *
 * Implements single-owner lease lifecycle, generation/epoch tracking to prevent
 * late-callback race conditions, non-overlapping HTTP keepalives, and fail-closed
 * input validation.
 */

export interface SessionConfig {
  deviceId: number;
  leaseId: string;
  streamInstanceId: string;
  keepaliveIntervalMs?: number;
}

export interface SessionCallbacks {
  onKeepaliveRequest: (deviceId: number, leaseId: string) => Promise<void>;
  onReleaseRequest: (deviceId: number, leaseId: string) => Promise<void>;
  onScopeChangeRequest?: (deviceId: number, scope: "stream" | "input") => Promise<void>;
  onSessionTerminated: (reason: string, details?: { code?: string; status?: number }) => void;
  onLeaseLost: (status: number, message: string) => void;
}

export interface StreamStateEvent {
  state?: string;
  reason?: string;
  stream_instance_id?: string;
}

export class SessionLifecycleCoordinator {
  private generation = 0;
  private currentSession: SessionConfig | null = null;
  private keepaliveTimer: ReturnType<typeof setTimeout> | null = null;
  private isRenewing = false;
  private isTearingDown = false;
  private consecutiveFailures = 0;
  private readonly maxConsecutiveFailures = 3;
  private readonly defaultIntervalMs = 5000;

  constructor(private readonly callbacks: SessionCallbacks) {}

  /**
   * Current generation/epoch number. Incremented on each session start or stop.
   */
  public get currentGeneration(): number {
    return this.generation;
  }

  /**
   * Active session configuration, or null if no session is active.
   */
  public get session(): SessionConfig | null {
    return this.currentSession;
  }

  /**
   * Whether a session is currently active.
   */
  public get isActive(): boolean {
    return this.currentSession !== null && !this.isTearingDown;
  }

  /**
   * Start a new video surveillance session.
   * Invalidates any previous generation and starts scheduled keepalives.
   */
  public startSession(config: SessionConfig): number {
    // Invalidate prior epoch and cancel previous timer
    this.generation += 1;
    const epoch = this.generation;

    this.clearKeepaliveTimer();
    this.currentSession = { ...config };
    this.isRenewing = false;
    this.isTearingDown = false;
    this.consecutiveFailures = 0;

    this.scheduleNextKeepalive(epoch, config.keepaliveIntervalMs ?? this.defaultIntervalMs);
    return epoch;
  }

  /**
   * Safely stops the session, cancels timers, invalidates the generation,
   * and executes a single release request for the lease.
   */
  public async stopSession(reason = "normal"): Promise<void> {
    if (!this.currentSession && !this.isRenewing) {
      return;
    }

    // Invalidate generation immediately to drop any in-flight keepalive responses
    this.generation += 1;
    this.isTearingDown = true;
    this.clearKeepaliveTimer();

    const sessionToRelease = this.currentSession;
    this.currentSession = null;
    this.isRenewing = false;

    if (sessionToRelease) {
      try {
        await this.callbacks.onReleaseRequest(sessionToRelease.deviceId, sessionToRelease.leaseId);
      } catch (err) {
        // Safe logging without credentials
        console.warn(`[SessionLifecycle gen=${this.generation}] Release request failed:`, err);
      }
    }

    this.isTearingDown = false;
  }

  /**
   * Detaches input control (e.g. operator toggled control off) WITHOUT
   * destroying the underlying stream lease.
   */
  public async detachInput(): Promise<void> {
    if (!this.currentSession || this.isTearingDown) {
      return;
    }

    // If scope downgrade is supported by BFF, notify it
    if (this.callbacks.onScopeChangeRequest) {
      try {
        await this.callbacks.onScopeChangeRequest(this.currentSession.deviceId, "stream");
      } catch (err) {
        console.warn(`[SessionLifecycle gen=${this.generation}] Scope downgrade warning:`, err);
      }
    }
    // Note: this.currentSession remains intact and keepalive continues ticking!
  }

  /**
   * Handles incoming stream state event from either WebSocket or REST polling.
   * Returns true if event was processed, false if discarded (e.g. stale epoch).
   */
  public handleStreamStateEvent(event: StreamStateEvent): boolean {
    if (!this.currentSession || this.isTearingDown) {
      return false;
    }

    // Check epoch / stream_instance_id matching
    if (
      event.stream_instance_id &&
      this.currentSession.streamInstanceId &&
      event.stream_instance_id !== this.currentSession.streamInstanceId
    ) {
      // Discard event from a different/older stream instance
      return false;
    }

    const state = event.state?.toLowerCase();
    if (state === "stopped" || state === "failed") {
      const reason = event.reason || state;
      const stoppedInstanceId = event.stream_instance_id || this.currentSession.streamInstanceId;

      // Stop session immediately without waiting for HTTP keepalive 404
      void this.stopSession(`terminal_${state}`);
      this.callbacks.onSessionTerminated(reason, {
        code: state,
      });
      return true;
    }

    return true;
  }

  private clearKeepaliveTimer(): void {
    if (this.keepaliveTimer) {
      clearTimeout(this.keepaliveTimer);
      this.keepaliveTimer = null;
    }
  }

  private scheduleNextKeepalive(epoch: number, delayMs: number): void {
    this.clearKeepaliveTimer();
    if (epoch !== this.generation || this.isTearingDown || !this.currentSession) {
      return;
    }

    this.keepaliveTimer = setTimeout(() => {
      void this.executeKeepaliveTick(epoch, delayMs);
    }, delayMs);
  }

  private async executeKeepaliveTick(epoch: number, intervalMs: number): Promise<void> {
    if (epoch !== this.generation || this.isTearingDown || !this.currentSession) {
      return;
    }

    // Prevent overlapping requests
    if (this.isRenewing) {
      this.scheduleNextKeepalive(epoch, 1000);
      return;
    }

    this.isRenewing = true;
    const { deviceId, leaseId } = this.currentSession;

    try {
      await this.callbacks.onKeepaliveRequest(deviceId, leaseId);

      // Verify epoch hasn't changed during the async request
      if (epoch !== this.generation || this.isTearingDown || !this.currentSession) {
        return;
      }

      this.consecutiveFailures = 0;
      this.isRenewing = false;
      this.scheduleNextKeepalive(epoch, intervalMs);
    } catch (error: any) {
      // Check generation again: if session was stopped or changed, ignore error completely
      if (epoch !== this.generation || this.isTearingDown || !this.currentSession) {
        this.isRenewing = false;
        return;
      }

      this.isRenewing = false;
      const status = error?.response?.status || error?.status;
      const detail =
        error?.response?.data?.detail ||
        error?.message ||
        "Lease expired or revoked upstream";

      if (status === 404 || status === 409 || status === 403) {
        // Fatal lease loss: single transition, cancel renewal, terminate session
        void this.stopSession(`lease_lost_${status}`);
        this.callbacks.onLeaseLost(status, typeof detail === "string" ? detail : `HTTP ${status}`);
        return;
      }

      // Transient error (e.g. timeout, 5xx): bounded retries
      this.consecutiveFailures += 1;
      if (this.consecutiveFailures >= this.maxConsecutiveFailures) {
        void this.stopSession("lease_timeout");
        this.callbacks.onLeaseLost(
          status || 504,
          `Превышено время ожидания продления аренды (${this.consecutiveFailures} попыток)`
        );
      } else {
        // Retry sooner on transient error (e.g. 2s)
        this.scheduleNextKeepalive(epoch, 2000);
      }
    }
  }
}

/**
 * Strict fail-closed input permission evaluator.
 * Pointer and keyboard events MUST only be sent if all three criteria pass:
 * 1. Control is explicitly active.
 * 2. Mode is strictly "desktop" (NOT undefined, NOT camera, NOT unknown).
 * 3. Geometry is known and valid (> 0 dimensions).
 */
export function isInputPermitted(
  streamMode: string | undefined | null,
  isControlActive: boolean,
  geometryKnown: boolean
): boolean {
  if (!isControlActive) return false;
  if (!streamMode || streamMode !== "desktop") return false;
  if (!geometryKnown) return false;
  return true;
}
