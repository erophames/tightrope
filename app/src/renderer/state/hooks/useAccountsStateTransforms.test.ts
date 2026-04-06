import { describe, expect, test, vi } from 'vitest';
import type { RuntimeAccount, RuntimeAccountTraffic } from '../../shared/types';
import type { AccountTrafficFrame } from './useTrafficStream';
import {
  applyTrafficFrameToAccounts,
  mapRuntimeAccountsWithTraffic,
  patchRuntimeAccountKeepingTraffic,
  runtimeAccountToUiAccount,
  runtimeTrafficRecordToFrame,
} from './useAccountsStateTransforms';

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

describe('useAccountsStateTransforms', () => {
  test('patchRuntimeAccountKeepingTraffic keeps existing traffic fields', () => {
    const initial = runtimeAccountToUiAccount(runtimeAccount({ accountId: 'acc_1', email: 'one@test.local' }));
    const withTraffic = {
      ...initial,
      usageRefreshStatus: 'auth_required' as const,
      usageRefreshMessage: 'code=token_expired',
      usageRefreshUpdatedAtMs: 1234,
      trafficUpBytes: 11,
      trafficDownBytes: 22,
      trafficLastUpAtMs: 33,
      trafficLastDownAtMs: 44,
    };

    const next = patchRuntimeAccountKeepingTraffic(
      [withTraffic],
      runtimeAccount({ accountId: 'acc_1', email: 'renamed@test.local', status: 'rate_limited' }),
    );

    expect(next[0].name).toBe('renamed@test.local');
    expect(next[0].state).toBe('rate_limited');
    expect(next[0].usageRefreshStatus).toBe('auth_required');
    expect(next[0].usageRefreshMessage).toBe('code=token_expired');
    expect(next[0].usageRefreshUpdatedAtMs).toBe(1234);
    expect(next[0].trafficUpBytes).toBe(11);
    expect(next[0].trafficDownBytes).toBe(22);
    expect(next[0].trafficLastUpAtMs).toBe(33);
    expect(next[0].trafficLastDownAtMs).toBe(44);
  });

  test('applyTrafficFrameToAccounts stores pending frame when account is unknown', () => {
    const known = runtimeAccountToUiAccount(runtimeAccount({ accountId: 'acc_1', email: 'one@test.local' }));
    const pending = new Map<string, AccountTrafficFrame>();
    const frame: AccountTrafficFrame = {
      accountId: 'acc_2',
      upBytes: 200,
      downBytes: 400,
      lastUpAtMs: 1000,
      lastDownAtMs: 2000,
    };

    const next = applyTrafficFrameToAccounts([known], frame, pending);

    expect(next).toHaveLength(1);
    expect(next[0].id).toBe('acc_1');
    expect(pending.get('acc_2')).toEqual(frame);
  });

  test('mapRuntimeAccountsWithTraffic carries previous and pending traffic values', () => {
    const previousKnown = {
      ...runtimeAccountToUiAccount(runtimeAccount({ accountId: 'acc_1', email: 'one@test.local' })),
      trafficUpBytes: 9,
      trafficDownBytes: 8,
      trafficLastUpAtMs: 7,
      trafficLastDownAtMs: 6,
    };
    const pending = new Map<string, AccountTrafficFrame>([
      [
        'acc_2',
        {
          accountId: 'acc_2',
          upBytes: 77,
          downBytes: 88,
          lastUpAtMs: 99,
          lastDownAtMs: 111,
        },
      ],
    ]);

    const mapped = mapRuntimeAccountsWithTraffic(
      [
        runtimeAccount({ accountId: 'acc_1', email: 'one@test.local' }),
        runtimeAccount({ accountId: 'acc_2', email: 'two@test.local' }),
      ],
      [previousKnown],
      pending,
    );

    expect(mapped[0].trafficUpBytes).toBe(9);
    expect(mapped[0].trafficDownBytes).toBe(8);
    expect(mapped[1].trafficUpBytes).toBe(77);
    expect(mapped[1].trafficDownBytes).toBe(88);
    expect(pending.has('acc_2')).toBe(false);
  });

  test('runtimeTrafficRecordToFrame maps traffic payload fields', () => {
    const record: RuntimeAccountTraffic = {
      accountId: 'acc_3',
      upBytes: 111,
      downBytes: 222,
      lastUpAtMs: 333,
      lastDownAtMs: 444,
    };

    expect(runtimeTrafficRecordToFrame(record)).toEqual({
      accountId: 'acc_3',
      upBytes: 111,
      downBytes: 222,
      lastUpAtMs: 333,
      lastDownAtMs: 444,
    });
  });

  test('applyTrafficFrameToAccounts marks activity on byte growth even when timestamps are stale', () => {
    const nowSpy = vi.spyOn(Date, 'now').mockReturnValue(5000);
    try {
      const account = {
        ...runtimeAccountToUiAccount(runtimeAccount({ accountId: 'acc_1', email: 'one@test.local' })),
        trafficUpBytes: 100,
        trafficDownBytes: 200,
        trafficLastUpAtMs: 1000,
        trafficLastDownAtMs: 2000,
      };
      const pending = new Map<string, AccountTrafficFrame>();
      const frame: AccountTrafficFrame = {
        accountId: 'acc_1',
        upBytes: 110,
        downBytes: 220,
        lastUpAtMs: 1000,
        lastDownAtMs: 2000,
      };

      const next = applyTrafficFrameToAccounts([account], frame, pending);

      expect(next[0].trafficUpBytes).toBe(110);
      expect(next[0].trafficDownBytes).toBe(220);
      expect(next[0].trafficLastUpAtMs).toBe(5000);
      expect(next[0].trafficLastDownAtMs).toBe(5000);
    } finally {
      nowSpy.mockRestore();
    }
  });
});
