import { act, renderHook, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { RuntimeAccount } from '../../shared/types';
import { useAccounts, type UseAccountsOptions } from './useAccounts';

function runtimeAccount(partial: Partial<RuntimeAccount> & Pick<RuntimeAccount, 'accountId' | 'email'>): RuntimeAccount {
  return {
    accountId: partial.accountId,
    email: partial.email,
    provider: partial.provider ?? 'openai',
    status: partial.status ?? 'active',
    routingPinned: partial.routingPinned ?? false,
    loadPercent: partial.loadPercent,
    quotaPrimaryPercent: partial.quotaPrimaryPercent,
    quotaSecondaryPercent: partial.quotaSecondaryPercent,
    quotaPrimaryWindowSeconds: partial.quotaPrimaryWindowSeconds,
    quotaSecondaryWindowSeconds: partial.quotaSecondaryWindowSeconds,
    quotaPrimaryResetAtMs: partial.quotaPrimaryResetAtMs,
    quotaSecondaryResetAtMs: partial.quotaSecondaryResetAtMs,
    inflight: partial.inflight,
    latencyMs: partial.latencyMs,
    errorEwma: partial.errorEwma,
    stickyHitPercent: partial.stickyHitPercent,
    requests24h: partial.requests24h,
    failovers24h: partial.failovers24h,
    costNorm: partial.costNorm,
    planType: partial.planType,
  };
}

type AccountsServiceMocks = UseAccountsOptions['service'];

function createAccountsServiceMocks(overrides: Partial<AccountsServiceMocks> = {}): AccountsServiceMocks {
  return {
    listAccountsRequest: vi.fn(async () => []),
    listAccountTrafficRequest: vi.fn(async () => []),
    refreshAccountUsageTelemetryRequest: vi.fn(async (accountId: string) =>
      runtimeAccount({ accountId, email: `${accountId}@test.local`, status: 'active' }),
    ),
    refreshAccountTokenRequest: vi.fn(async (accountId: string) =>
      runtimeAccount({ accountId, email: `${accountId}@test.local`, status: 'active' }),
    ),
    pinAccountRequest: vi.fn(async () => {}),
    unpinAccountRequest: vi.fn(async () => {}),
    pauseAccountRequest: vi.fn(async () => {}),
    reactivateAccountRequest: vi.fn(async () => {}),
    deleteAccountRequest: vi.fn(async () => {}),
    ...overrides,
  };
}

describe('useAccounts', () => {
  afterEach(() => {
    vi.useRealTimers();
  });

  it('maps runtime accounts on refresh', async () => {
    const listAccountsRequest = vi.fn(async () => [
      runtimeAccount({ accountId: 'acc_1', email: 'one@test.local', routingPinned: true, status: 'active' }),
    ]);
    const service = createAccountsServiceMocks({ listAccountsRequest });

    const { result } = renderHook(() =>
      useAccounts(
        { runtimeBind: '127.0.0.1:2455', pushRuntimeEvent: vi.fn(), service },
        { enableWebSocket: false, accountsRefreshMs: 60_000, trafficSnapshotPollMs: 60_000, trafficClockTickMs: 60_000 },
      ),
    );

    await act(async () => {
      await result.current.refreshAccountsFromNative();
    });

    expect(result.current.accounts).toHaveLength(1);
    expect(result.current.accounts[0].id).toBe('acc_1');
    expect(result.current.accounts[0].name).toBe('one@test.local');
    expect(result.current.accounts[0].pinned).toBe(true);
  });

  it('pins an account and refreshes state', async () => {
    const listAccountsRequest = vi
      .fn()
      .mockResolvedValueOnce([
        runtimeAccount({ accountId: 'acc_1', email: 'one@test.local', routingPinned: false, status: 'active' }),
      ])
      .mockResolvedValueOnce([
        runtimeAccount({ accountId: 'acc_1', email: 'one@test.local', routingPinned: true, status: 'active' }),
      ]);
    const pinAccountRequest = vi.fn(async () => {});
    const service = createAccountsServiceMocks({ listAccountsRequest, pinAccountRequest });

    const { result } = renderHook(() =>
      useAccounts(
        { runtimeBind: '127.0.0.1:2455', pushRuntimeEvent: vi.fn(), service },
        { enableWebSocket: false, accountsRefreshMs: 60_000, trafficSnapshotPollMs: 60_000, trafficClockTickMs: 60_000 },
      ),
    );

    await act(async () => {
      await result.current.refreshAccountsFromNative();
    });

    await act(async () => {
      await result.current.toggleAccountPin('acc_1', true);
    });

    expect(pinAccountRequest).toHaveBeenCalledWith('acc_1');
    expect(result.current.accounts[0].pinned).toBe(true);
  });

  it('preserves traffic timestamps when account status changes', async () => {
    const listAccountsRequest = vi
      .fn()
      .mockResolvedValueOnce([
        runtimeAccount({ accountId: 'acc_1', email: 'one@test.local', routingPinned: false, status: 'active' }),
      ])
      .mockResolvedValueOnce([
        runtimeAccount({ accountId: 'acc_1', email: 'one@test.local', routingPinned: false, status: 'rate_limited' }),
      ]);
    const listAccountTrafficRequest = vi.fn(async () => [
      {
        accountId: 'acc_1',
        upBytes: 1200,
        downBytes: 480,
        lastUpAtMs: 12345,
        lastDownAtMs: 23456,
      },
    ]);
    const service = createAccountsServiceMocks({ listAccountsRequest, listAccountTrafficRequest });

    const { result } = renderHook(() =>
      useAccounts(
        { runtimeBind: '127.0.0.1:2455', pushRuntimeEvent: vi.fn(), service },
        { enableWebSocket: false, accountsRefreshMs: 60_000, trafficSnapshotPollMs: 60_000, trafficClockTickMs: 60_000 },
      ),
    );

    await act(async () => {
      await result.current.refreshAccountsFromNative();
      await result.current.refreshAccountTrafficSnapshot();
    });

    const appliedUpAtMs = result.current.accounts[0].trafficLastUpAtMs ?? 0;
    const appliedDownAtMs = result.current.accounts[0].trafficLastDownAtMs ?? 0;
    expect(appliedUpAtMs).toBeGreaterThan(0);
    expect(appliedDownAtMs).toBeGreaterThan(0);

    await act(async () => {
      await result.current.refreshAccountsFromNative();
    });

    expect(result.current.accounts[0].state).toBe('rate_limited');
    expect(result.current.accounts[0].trafficLastUpAtMs).toBe(appliedUpAtMs);
    expect(result.current.accounts[0].trafficLastDownAtMs).toBe(appliedDownAtMs);
  });

  it('deletes an account and returns success', async () => {
    const listAccountsRequest = vi
      .fn()
      .mockResolvedValueOnce([
        runtimeAccount({ accountId: 'acc_1', email: 'one@test.local', routingPinned: false, status: 'active' }),
      ])
      .mockResolvedValueOnce([]);
    const deleteAccountRequest = vi.fn(async () => {});
    const service = createAccountsServiceMocks({ listAccountsRequest, deleteAccountRequest });

    const { result } = renderHook(() =>
      useAccounts(
        { runtimeBind: '127.0.0.1:2455', pushRuntimeEvent: vi.fn(), service },
        { enableWebSocket: false, accountsRefreshMs: 60_000, trafficSnapshotPollMs: 60_000, trafficClockTickMs: 60_000 },
      ),
    );

    await act(async () => {
      await result.current.refreshAccountsFromNative();
    });

    let deleted = false;
    await act(async () => {
      deleted = await result.current.deleteAccount('acc_1');
    });

    expect(deleted).toBe(true);
    expect(deleteAccountRequest).toHaveBeenCalledWith('acc_1');
    expect(result.current.accounts).toHaveLength(0);
  });

  it('includes account name and id in refresh usage telemetry failure events', async () => {
    const listAccountsRequest = vi.fn(async () => [
      runtimeAccount({ accountId: 'acc_1', email: 'one@test.local', routingPinned: false, status: 'active' }),
    ]);
    const refreshAccountUsageTelemetryRequest = vi
      .fn()
      .mockRejectedValue(new Error('Failed to fetch usage telemetry from provider (HTTP 401): code=token_expired'));
    const pushRuntimeEvent = vi.fn();
    const service = createAccountsServiceMocks({ listAccountsRequest, refreshAccountUsageTelemetryRequest });

    const { result } = renderHook(() =>
      useAccounts(
        { runtimeBind: '127.0.0.1:2455', pushRuntimeEvent, service },
        { enableWebSocket: false, accountsRefreshMs: 60_000, trafficSnapshotPollMs: 60_000, trafficClockTickMs: 60_000 },
      ),
    );

    await act(async () => {
      await result.current.refreshAccountsFromNative();
    });
    await waitFor(() => {
      expect(result.current.accounts).toHaveLength(1);
    });
    await act(async () => {
      await result.current.refreshAccountTelemetry('acc_1');
    });

    const warningMessages = pushRuntimeEvent.mock.calls
      .filter(([, level]) => level === 'warn')
      .map(([message]) => String(message));
    expect(
      warningMessages.some(
        (message) =>
          message.includes('one@test.local (acc_1)') && message.includes('code=token_expired'),
      ),
    ).toBe(true);
  });

  it('marks account token as needing refresh on token expiry and clears after direct token refresh', async () => {
    const listAccountsRequest = vi.fn(async () => [
      runtimeAccount({ accountId: 'acc_1', email: 'one@test.local', routingPinned: false, status: 'active' }),
    ]);
    const refreshAccountUsageTelemetryRequest = vi
      .fn()
      .mockRejectedValue(new Error('Failed to fetch usage telemetry from provider (HTTP 401): code=token_expired'));
    const refreshAccountTokenRequest = vi.fn(async (accountId: string) =>
      runtimeAccount({ accountId, email: 'one@test.local', routingPinned: false, status: 'active' }),
    );
    const service = createAccountsServiceMocks({
      listAccountsRequest,
      refreshAccountUsageTelemetryRequest,
      refreshAccountTokenRequest,
    });

    const { result } = renderHook(() =>
      useAccounts(
        { runtimeBind: '127.0.0.1:2455', pushRuntimeEvent: vi.fn(), service },
        { enableWebSocket: false, accountsRefreshMs: 60_000, trafficSnapshotPollMs: 60_000, trafficClockTickMs: 60_000 },
      ),
    );

    await act(async () => {
      await result.current.refreshAccountsFromNative();
    });

    await act(async () => {
      await result.current.refreshAccountTelemetry('acc_1');
    });
    await waitFor(() => {
      expect(result.current.accounts[0]?.needsTokenRefresh).toBe(true);
    });

    await act(async () => {
      await result.current.refreshAccountToken('acc_1');
    });

    expect(refreshAccountTokenRequest).toHaveBeenCalledWith('acc_1');
    await waitFor(() => {
      expect(result.current.accounts[0]?.needsTokenRefresh).toBe(false);
    });
  });

  it('stores per-account refresh-all status and auth requirement from refresh results', async () => {
    const listAccountsRequest = vi.fn(async () => [
      runtimeAccount({ accountId: 'acc_auth', email: 'auth@test.local', routingPinned: false, status: 'active' }),
      runtimeAccount({ accountId: 'acc_fail', email: 'fail@test.local', routingPinned: false, status: 'active' }),
    ]);
    const refreshAccountUsageTelemetryRequest = vi.fn(async (accountId: string) => {
      if (accountId === 'acc_auth') {
        throw new Error('Failed to fetch usage telemetry from provider (HTTP 401): code=token_expired');
      }
      if (accountId === 'acc_fail') {
        throw new Error('Failed to fetch usage telemetry from provider (HTTP 500): code=upstream_unavailable');
      }
      return runtimeAccount({ accountId, email: `${accountId}@test.local`, status: 'active' });
    });
    const service = createAccountsServiceMocks({
      listAccountsRequest,
      refreshAccountUsageTelemetryRequest,
    });

    const { result } = renderHook(() =>
      useAccounts(
        { runtimeBind: '127.0.0.1:2455', pushRuntimeEvent: vi.fn(), service },
        { enableWebSocket: false, accountsRefreshMs: 60_000, trafficSnapshotPollMs: 60_000, trafficClockTickMs: 60_000 },
      ),
    );

    await act(async () => {
      await result.current.refreshAccountsFromNative();
    });
    await waitFor(() => {
      expect(result.current.accounts).toHaveLength(2);
    });
    await act(async () => {
      await result.current.refreshAllAccountsTelemetry();
    });

    const auth = result.current.accounts.find((account) => account.id === 'acc_auth');
    const fail = result.current.accounts.find((account) => account.id === 'acc_fail');
    expect(auth?.usageRefreshStatus).toBe('auth_required');
    expect(auth?.needsTokenRefresh).toBe(true);
    expect(auth?.usageRefreshMessage).toContain('token_expired');
    expect(fail?.usageRefreshStatus).toBe('failed');
    expect(fail?.needsTokenRefresh).toBe(false);
    expect(fail?.usageRefreshMessage).toContain('upstream_unavailable');
  });

  it('reports account polling error once until recovery', async () => {
    vi.useFakeTimers();
    const pushRuntimeEvent = vi.fn();
    const listAccountsRequest = vi.fn().mockRejectedValue(new Error('down'));
    const service = createAccountsServiceMocks({ listAccountsRequest });

    const { result } = renderHook(() =>
      useAccounts(
        { runtimeBind: '127.0.0.1:2455', pushRuntimeEvent, service },
        { enableWebSocket: false, accountsRefreshMs: 10, trafficSnapshotPollMs: 60_000, trafficClockTickMs: 60_000 },
      ),
    );

    await act(async () => {
      vi.advanceTimersByTime(30);
      await Promise.resolve();
    });

    expect(pushRuntimeEvent).toHaveBeenCalledWith('account polling failed; retrying', 'warn');
    expect(pushRuntimeEvent).toHaveBeenCalledTimes(1);

    await act(async () => {
      listAccountsRequest.mockResolvedValueOnce([]);
      await result.current.refreshAccountsFromNative();
      listAccountsRequest.mockRejectedValueOnce(new Error('down'));
    });

    await act(async () => {
      vi.advanceTimersByTime(10);
      await Promise.resolve();
    });

    expect(pushRuntimeEvent).toHaveBeenCalledTimes(2);
    expect(pushRuntimeEvent).toHaveBeenLastCalledWith('account polling failed; retrying', 'warn');
  });

  it('auto-refreshes telemetry when exhausted quota reset has elapsed', async () => {
    const nowMs = Date.now();
    const listAccountsRequest = vi.fn(async () => [
      runtimeAccount({
        accountId: 'acc_1',
        email: 'one@test.local',
        status: 'rate_limited',
        quotaPrimaryPercent: 100,
        quotaPrimaryResetAtMs: nowMs - 5_000,
      }),
    ]);
    const refreshAccountUsageTelemetryRequest = vi.fn(async (accountId: string) =>
      runtimeAccount({ accountId, email: `${accountId}@test.local`, status: 'active' }),
    );
    const pushRuntimeEvent = vi.fn();
    const service = createAccountsServiceMocks({ listAccountsRequest, refreshAccountUsageTelemetryRequest });

    const { result } = renderHook(() =>
      useAccounts(
        { runtimeBind: '127.0.0.1:2455', pushRuntimeEvent, service },
        { enableWebSocket: false, accountsRefreshMs: 60_000, trafficSnapshotPollMs: 60_000, trafficClockTickMs: 60_000 },
      ),
    );

    await act(async () => {
      await result.current.refreshAccountsFromNative();
    });

    await waitFor(() => {
      expect(refreshAccountUsageTelemetryRequest).toHaveBeenCalledWith('acc_1');
    });
    expect(pushRuntimeEvent).toHaveBeenCalledWith(
      'usage telemetry auto-refreshed after reset: one@test.local',
      'success',
    );
  });
});
