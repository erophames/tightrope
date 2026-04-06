import { renderHook } from '@testing-library/react';
import { describe, expect, test } from 'vitest';
import { createInitialRuntimeState } from '../../test/fixtures/seed';
import type { Account } from '../../shared/types';
import { useAccountsDerivedView } from './useAccountsDerivedView';

function makeAccount(overrides: Partial<Account> = {}): Account {
  return {
    id: 'acc-default',
    name: 'default@test.local',
    pinned: false,
    plan: 'free',
    health: 'healthy',
    state: 'active',
    inflight: 0,
    load: 0,
    latency: 0,
    errorEwma: 0,
    cooldown: false,
    capability: true,
    costNorm: 0,
    routed24h: 0,
    stickyHit: 0,
    quotaPrimary: 0,
    quotaSecondary: 0,
    failovers: 0,
    note: 'openai',
    telemetryBacked: false,
    trafficUpBytes: 0,
    trafficDownBytes: 0,
    trafficLastUpAtMs: 0,
    trafficLastDownAtMs: 0,
    ...overrides,
  };
}

describe('useAccountsDerivedView', () => {
  test('filters accounts and projects selected account state', () => {
    const state = createInitialRuntimeState();
    state.accountSearchQuery = 'bob';
    state.accountStatusFilter = 'active';
    state.selectedAccountDetailId = 'acc-bob';

    const accounts: Account[] = [
      makeAccount({ id: 'acc-bob', name: 'bob@test.local', state: 'active' }),
      makeAccount({ id: 'acc-alice', name: 'alice@test.local', state: 'paused' }),
    ];

    const { result } = renderHook(() =>
      useAccountsDerivedView({
        state,
        accounts,
        isRefreshingAccountTelemetry: (accountId) => accountId === 'acc-bob',
      }),
    );

    expect(result.current.filteredAccounts).toHaveLength(1);
    expect(result.current.filteredAccounts[0].id).toBe('acc-bob');
    expect(result.current.selectedAccountDetail?.id).toBe('acc-bob');
    expect(result.current.isRefreshingSelectedAccountTelemetry).toBe(true);
    expect(result.current.selectedAccountUsage24h).toEqual({
      requests: expect.any(Number),
      tokens: expect.any(Number),
      costUsd: expect.any(Number),
      failovers: expect.any(Number),
    });
  });

  test('returns null selection and non-refreshing state when selected account is missing', () => {
    const state = createInitialRuntimeState();
    state.selectedAccountDetailId = 'acc-missing';
    const accounts: Account[] = [makeAccount({ id: 'acc-a' })];

    const { result } = renderHook(() =>
      useAccountsDerivedView({
        state,
        accounts,
        isRefreshingAccountTelemetry: (accountId) => accountId === 'acc-a',
      }),
    );

    expect(result.current.selectedAccountDetail).toBeNull();
    expect(result.current.isRefreshingSelectedAccountTelemetry).toBe(false);
  });
});
