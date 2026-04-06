import { act, renderHook } from '@testing-library/react';
import { useState } from 'react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { createInitialRuntimeState } from '../../test/fixtures/seed';
import type { RouteRow, RuntimeRequestLog } from '../../shared/types';
import { useRequestLog } from './useRequestLog';

afterEach(() => {
  vi.useRealTimers();
});

describe('useRequestLog', () => {
  it('refreshes rows and triggers fast account refresh when needed', async () => {
    const triggerFastAccountRefresh = vi.fn();
    const listRequestLogs = vi.fn().mockResolvedValue([
      {
        id: 1,
        accountId: 'acc_1',
        path: '/v1/responses',
        method: 'POST',
        statusCode: 429,
        model: 'gpt-5',
        requestedAt: '2026-04-02 12:00:00',
        errorCode: 'rate_limit',
        transport: 'sse',
        totalCost: 0,
        routingStrategy: 'weighted_round_robin',
        latencyMs: 42,
        totalTokens: 12,
      } satisfies RuntimeRequestLog,
    ]);

    const { result } = renderHook(() => {
      const [state, setState] = useState(createInitialRuntimeState());
      const requestLog = useRequestLog({
        refreshMs: 60_000,
        requestLogsLimit: 250,
        setState,
        mapRuntimeRequestLog: (log): RouteRow => ({
          id: `log_${log.id}`,
          time: '12:00:00',
          model: log.model ?? '—',
          accountId: log.accountId ?? '',
          tokens: 12,
          latency: 42,
          status: 'warn',
          protocol: 'SSE',
          sessionId: log.path,
          sticky: false,
        }),
        shouldTriggerImmediateAccountsRefresh: () => true,
        triggerFastAccountRefresh,
        listRequestLogsRequest: listRequestLogs,
      });

      return { state, ...requestLog };
    });

    await act(async () => {
      await result.current.refreshRequestLogs();
    });

    expect(result.current.state.rows).toHaveLength(1);
    expect(result.current.state.rows[0].id).toBe('log_1');
    expect(triggerFastAccountRefresh).toHaveBeenCalledTimes(1);
  });

  it('reports polling error only once until a successful refresh', async () => {
    vi.useFakeTimers();
    const reportPollingError = vi.fn();
    const triggerFastAccountRefresh = vi.fn();
    const listRequestLogs = vi.fn().mockRejectedValue(new Error('runtime unavailable'));

    const { result } = renderHook(() => {
      const [, setState] = useState(createInitialRuntimeState());
      return useRequestLog({
        refreshMs: 10,
        requestLogsLimit: 250,
        setState,
        mapRuntimeRequestLog: (log): RouteRow => ({
          id: `log_${log.id}`,
          time: '12:00:00',
          model: log.model ?? '—',
          accountId: log.accountId ?? '',
          tokens: 0,
          latency: 0,
          status: 'ok',
          protocol: 'SSE',
          sessionId: '',
          sticky: false,
        }),
        shouldTriggerImmediateAccountsRefresh: () => false,
        triggerFastAccountRefresh,
        reportPollingError,
        listRequestLogsRequest: listRequestLogs,
      });
    });

    await act(async () => {
      vi.advanceTimersByTime(30);
      await Promise.resolve();
    });

    expect(reportPollingError).toHaveBeenCalledTimes(1);

    await act(async () => {
      listRequestLogs.mockResolvedValueOnce([]);
      await result.current.refreshRequestLogs();
      listRequestLogs.mockRejectedValueOnce(new Error('runtime unavailable'));
    });

    await act(async () => {
      vi.advanceTimersByTime(10);
      await Promise.resolve();
    });

    expect(reportPollingError).toHaveBeenCalledTimes(2);
  });
});
