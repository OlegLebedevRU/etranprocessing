import { describe, it, expect, vi } from 'vitest';
import { SessionLifecycleCoordinator } from '../utils/sessionLifecycle';

describe('HEVC watcher ownership and failure', () => {
  it('watcher neither renews nor downgrades producer and leaves with no ownership', async () => {
    vi.useFakeTimers();
    const keepalive = vi.fn(); const scope = vi.fn(); const release = vi.fn();
    const session = new SessionLifecycleCoordinator({ onKeepaliveRequest: keepalive, onReleaseRequest: release, onScopeChangeRequest: scope, onSessionTerminated: vi.fn(), onLeaseLost: vi.fn() });
    session.startSession({ deviceId: 1, leaseId: '', streamInstanceId: 'real-epoch', publisherOwned: false });
    await vi.advanceTimersByTimeAsync(30000);
    await session.detachInput();
    expect(keepalive).not.toHaveBeenCalled(); expect(scope).not.toHaveBeenCalled();
    await session.stopSession();
    expect(release).toHaveBeenCalledWith(1, '', false);
    vi.useRealTimers();
  });
  it('ignores another epoch and terminates current HEVC stream without restarting', () => {
    const ended = vi.fn(); const keepalive = vi.fn();
    const session = new SessionLifecycleCoordinator({ onKeepaliveRequest: keepalive, onReleaseRequest: vi.fn(), onSessionTerminated: ended, onLeaseLost: vi.fn() });
    session.startSession({ deviceId: 1, leaseId: '', streamInstanceId: 'current', publisherOwned: false });
    expect(session.handleStreamStateEvent({state:'failed', stream_instance_id:'old'})).toBe(false);
    expect(ended).not.toHaveBeenCalled();
    expect(session.handleStreamStateEvent({state:'failed',stream_instance_id:'current',error:{codec:'h265',code:'encoder_failed'}})).toBe(true);
    expect(ended.mock.calls[0][0]).toContain('encoder_failed');
    expect(session.isActive).toBe(false); expect(keepalive).not.toHaveBeenCalled();
  });
});
