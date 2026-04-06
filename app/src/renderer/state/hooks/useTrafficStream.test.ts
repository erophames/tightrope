import { act, renderHook } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { useTrafficStream } from './useTrafficStream';

afterEach(() => {
  vi.useRealTimers();
});

describe('useTrafficStream', () => {
  it('updates traffic clock and polls snapshots', async () => {
    vi.useFakeTimers();
    const refreshSnapshot = vi.fn().mockResolvedValue(undefined);

    const { result } = renderHook(() =>
      useTrafficStream({
        runtimeBind: '127.0.0.1:2455',
        enableWebSocket: false,
        trafficReconnectMs: 1200,
        trafficClockTickMs: 100,
        trafficSnapshotPollMs: 200,
        trafficActiveWindowMs: 3000,
        onTrafficFrame: vi.fn(),
        refreshSnapshot,
      }),
    );

    const initialClock = result.current.trafficClockMs;

    await act(async () => {
      vi.advanceTimersByTime(250);
      await Promise.resolve();
    });

    expect(refreshSnapshot).toHaveBeenCalledTimes(1);
    expect(result.current.trafficClockMs).toBeGreaterThanOrEqual(initialClock);
  });

  it('sends snapshot request and refreshes when websocket opens', async () => {
    const originalWebSocket = globalThis.WebSocket;
    const refreshSnapshot = vi.fn().mockResolvedValue(undefined);

    class FakeWebSocket {
      static instances: FakeWebSocket[] = [];
      onopen: (() => void) | null = null;
      onmessage: ((event: MessageEvent) => void) | null = null;
      onclose: (() => void) | null = null;
      onerror: (() => void) | null = null;
      binaryType = '';
      send = vi.fn();
      close = vi.fn();

      constructor(_url: string) {
        FakeWebSocket.instances.push(this);
      }
    }

    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    (globalThis as any).WebSocket = FakeWebSocket;

    try {
      renderHook(() =>
        useTrafficStream({
          runtimeBind: '127.0.0.1:2455',
          enableWebSocket: true,
          trafficReconnectMs: 1200,
          trafficClockTickMs: 1000,
          trafficSnapshotPollMs: 60_000,
          trafficActiveWindowMs: 3000,
          onTrafficFrame: vi.fn(),
          refreshSnapshot,
        }),
      );

      const ws = FakeWebSocket.instances[0];
      expect(ws).toBeDefined();

      await act(async () => {
        ws.onopen?.();
        await Promise.resolve();
      });

      expect(ws.send).toHaveBeenCalledWith('snapshot');
      expect(refreshSnapshot).toHaveBeenCalled();
    } finally {
      // eslint-disable-next-line @typescript-eslint/no-explicit-any
      (globalThis as any).WebSocket = originalWebSocket;
    }
  });

  it('normalizes wildcard runtime bind host to loopback for websocket traffic stream', async () => {
    const originalWebSocket = globalThis.WebSocket;
    const refreshSnapshot = vi.fn().mockResolvedValue(undefined);
    const capturedUrls: string[] = [];

    class FakeWebSocket {
      onopen: (() => void) | null = null;
      onmessage: ((event: MessageEvent) => void) | null = null;
      onclose: (() => void) | null = null;
      onerror: (() => void) | null = null;
      binaryType = '';
      send = vi.fn();
      close = vi.fn();

      constructor(url: string) {
        capturedUrls.push(url);
      }
    }

    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    (globalThis as any).WebSocket = FakeWebSocket;

    try {
      renderHook(() =>
        useTrafficStream({
          runtimeBind: '0.0.0.0:3210',
          enableWebSocket: true,
          trafficReconnectMs: 1200,
          trafficClockTickMs: 1000,
          trafficSnapshotPollMs: 60_000,
          trafficActiveWindowMs: 3000,
          onTrafficFrame: vi.fn(),
          refreshSnapshot,
        }),
      );

      expect(capturedUrls[0]).toContain('127.0.0.1:3210');
      expect(capturedUrls[0]).toContain('/api/accounts/traffic/ws');
    } finally {
      // eslint-disable-next-line @typescript-eslint/no-explicit-any
      (globalThis as any).WebSocket = originalWebSocket;
    }
  });

  it('reports snapshot polling errors once while failures persist', async () => {
    vi.useFakeTimers();
    const reportPollingError = vi.fn();
    const refreshSnapshot = vi.fn().mockRejectedValue(new Error('down'));

    renderHook(() =>
      useTrafficStream({
        runtimeBind: '127.0.0.1:2455',
        enableWebSocket: false,
        trafficReconnectMs: 1200,
        trafficClockTickMs: 1000,
        trafficSnapshotPollMs: 10,
        trafficActiveWindowMs: 3000,
        onTrafficFrame: vi.fn(),
        refreshSnapshot,
        reportPollingError,
      }),
    );

    await act(async () => {
      vi.advanceTimersByTime(30);
      await Promise.resolve();
    });

    expect(reportPollingError).toHaveBeenCalledTimes(1);

    await act(async () => {
      vi.advanceTimersByTime(30);
      await Promise.resolve();
    });

    expect(reportPollingError).toHaveBeenCalledTimes(1);
  });
});
