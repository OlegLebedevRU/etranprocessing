/** One user action, one original budget. Cancellation never releases ownership early. */
export class CameraAttempt {
  readonly controller = new AbortController();
  timedOut = false;
  private timer: ReturnType<typeof setTimeout> | undefined;
  constructor(timeoutMs: number | null = 30000) {
    // Desktop requests and Janus transactions own their individual deadlines.
    // A cancellation-only guard must not shorten START by lease acquisition time.
    if (timeoutMs !== null) this.timer = setTimeout(() => { this.timedOut = true; this.cancel(); }, timeoutMs);
  }
  get signal() { return this.controller.signal; }
  check() { this.signal.throwIfAborted(); }
  cancel() { this.controller.abort(); clearTimeout(this.timer); }
  finish() { clearTimeout(this.timer); }
  async wait<T>(work: Promise<T>): Promise<T> {
    this.check();
    let abort: () => void = () => {};
    try {
      const value = await Promise.race([work, new Promise<never>((_, reject) => {
        abort = () => reject(new DOMException("Запуск отменён или истекло время ожидания", "AbortError"));
        this.signal.addEventListener("abort", abort, { once: true });
      })]);
      this.check();
      return value;
    } finally { this.signal.removeEventListener("abort", abort); }
  }
}
