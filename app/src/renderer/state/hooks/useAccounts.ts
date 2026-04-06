import { useCallback, useEffect, useRef, useState } from 'react';
import type { TightropeService } from '../../services/tightrope';
import type { Account, AccountUsageRefreshStatus, RuntimeAccount, RuntimeAccountTraffic } from '../../shared/types';
import type { StatusNoticeLevel } from '../statusNotices';
import { useDedupedRefresh } from './useDedupedRefresh';
import { useMountedFlag } from './useMountedFlag';
import { useTrafficStream, type AccountTrafficFrame } from './useTrafficStream';
import {
  deleteAccountAction,
  pauseAccountAction,
  reactivateAccountAction,
  refreshAccountTelemetryAction,
  refreshAccountTokenAction,
  refreshAllAccountsTelemetryAction,
  refreshUsageTelemetryAfterAccountAdd,
  toggleAccountPinAction,
} from './useAccountsMutationActions';
import {
  reportAccountPollingFailureOnce,
  reportFastAccountRefreshFailureOnce,
  resetAccountRefreshErrorFlags,
  tryStartFastAccountRefresh,
} from './useAccountsPolling';
import {
  applyTrafficFrameToAccounts,
  mapRuntimeAccountsWithTraffic,
  patchRuntimeAccountKeepingTraffic,
  runtimeTrafficRecordToFrame,
} from './useAccountsStateTransforms';

const DEFAULT_ACCOUNTS_REFRESH_MS = 1000;
const DEFAULT_FAST_REFRESH_COOLDOWN_MS = 250;
const DEFAULT_TRAFFIC_RECONNECT_MS = 1200;
const DEFAULT_TRAFFIC_CLOCK_TICK_MS = 200;
const DEFAULT_TRAFFIC_SNAPSHOT_POLL_MS = 1000;
const DEFAULT_TRAFFIC_ACTIVE_WINDOW_MS = 7000;
const AUTO_RESET_TELEMETRY_RETRY_MS = 60_000;
const AUTO_RESET_TELEMETRY_BATCH_SIZE = 2;

export interface UseAccountsOptions {
  runtimeBind: string;
  pushRuntimeEvent: (text: string, level?: StatusNoticeLevel) => void;
  service: Pick<
    TightropeService,
    | 'listAccountsRequest'
    | 'listAccountTrafficRequest'
    | 'refreshAccountUsageTelemetryRequest'
    | 'refreshAccountTokenRequest'
    | 'pinAccountRequest'
    | 'unpinAccountRequest'
    | 'pauseAccountRequest'
    | 'reactivateAccountRequest'
    | 'deleteAccountRequest'
  >;
}

interface UseAccountsConfig {
  accountsRefreshMs?: number;
  fastRefreshCooldownMs?: number;
  trafficReconnectMs?: number;
  trafficClockTickMs?: number;
  trafficSnapshotPollMs?: number;
  trafficActiveWindowMs?: number;
  enableWebSocket?: boolean;
}

interface UseAccountsResult {
  accounts: Account[];
  trafficClockMs: number;
  trafficActiveWindowMs: number;
  isRefreshingAllAccountTelemetry: boolean;
  refreshAccountsFromNative: () => Promise<RuntimeAccount[]>;
  refreshAccountTrafficSnapshot: () => Promise<void>;
  applyTrafficFrame: (frame: AccountTrafficFrame) => void;
  triggerFastAccountRefresh: () => void;
  refreshUsageTelemetryAfterAccountAdd: (accountId: string, accountName: string) => Promise<void>;
  toggleAccountPin: (accountId: string, nextPinned: boolean) => Promise<void>;
  pauseAccount: (accountId: string) => Promise<void>;
  reactivateAccount: (accountId: string) => Promise<void>;
  deleteAccount: (accountId: string) => Promise<boolean>;
  refreshAccountTelemetry: (accountId: string) => Promise<void>;
  refreshAllAccountsTelemetry: () => Promise<void>;
  refreshAccountToken: (accountId: string) => Promise<void>;
  isRefreshingAccountTelemetry: (accountId: string | null | undefined) => boolean;
  isRefreshingAccountToken: (accountId: string | null | undefined) => boolean;
}

function hasExhaustedQuotaReadyForRefresh(account: Account, nowMs: number): boolean {
  if (account.state === 'deactivated' || account.state === 'paused') {
    return false;
  }

  const windows: Array<{ usedPercent: number; resetAtMs: number | null | undefined }> = [];
  if (account.hasPrimaryQuota) {
    windows.push({ usedPercent: account.quotaPrimary, resetAtMs: account.quotaPrimaryResetAtMs });
  }
  if (account.hasSecondaryQuota) {
    windows.push({ usedPercent: account.quotaSecondary, resetAtMs: account.quotaSecondaryResetAtMs });
  }

  return windows.some((window) => {
    if (!Number.isFinite(window.usedPercent) || Math.round(window.usedPercent) < 100) {
      return false;
    }
    if (typeof window.resetAtMs !== 'number' || !Number.isFinite(window.resetAtMs) || window.resetAtMs <= 0) {
      return false;
    }
    return nowMs >= window.resetAtMs;
  });
}

export function useAccounts(options: UseAccountsOptions, config: UseAccountsConfig = {}): UseAccountsResult {
  const service = options.service;
  const {
    accountsRefreshMs = DEFAULT_ACCOUNTS_REFRESH_MS,
    fastRefreshCooldownMs = DEFAULT_FAST_REFRESH_COOLDOWN_MS,
    trafficReconnectMs = DEFAULT_TRAFFIC_RECONNECT_MS,
    trafficClockTickMs = DEFAULT_TRAFFIC_CLOCK_TICK_MS,
    trafficSnapshotPollMs = DEFAULT_TRAFFIC_SNAPSHOT_POLL_MS,
    trafficActiveWindowMs = DEFAULT_TRAFFIC_ACTIVE_WINDOW_MS,
    enableWebSocket = true,
  } = config;

  const [accounts, setAccounts] = useState<Account[]>([]);
  const [refreshingAccountTelemetryId, setRefreshingAccountTelemetryId] = useState<string | null>(null);
  const [refreshingAccountTokenId, setRefreshingAccountTokenId] = useState<string | null>(null);
  const [refreshingAllAccountTelemetry, setRefreshingAllAccountTelemetry] = useState(false);
  const mountedRef = useMountedFlag();

  const accountsRef = useRef<Account[]>([]);
  const pendingTrafficByAccountRef = useRef<Map<string, AccountTrafficFrame>>(new Map());
  const lastFastAccountRefreshMsRef = useRef(0);
  const accountsPollErrorReportedRef = useRef(false);
  const fastRefreshErrorReportedRef = useRef(false);
  const autoResetTelemetryInFlightRef = useRef<Set<string>>(new Set());
  const autoResetTelemetryLastAttemptMsRef = useRef<Map<string, number>>(new Map());

  useEffect(() => {
    accountsRef.current = accounts;
  }, [accounts]);

  const applyRuntimeAccountPatch = useCallback((record: RuntimeAccount): void => {
    if (!mountedRef.current) {
      return;
    }
    setAccounts((previous) => patchRuntimeAccountKeepingTraffic(previous, record));
  }, [mountedRef]);

  const setTokenRefreshRequired = useCallback((accountId: string, required: boolean): void => {
    if (!mountedRef.current) {
      return;
    }
    setAccounts((previous) => {
      const index = previous.findIndex((account) => account.id === accountId);
      if (index < 0) {
        return previous;
      }
      const current = previous[index];
      const currentValue = current.needsTokenRefresh === true;
      if (currentValue === required) {
        return previous;
      }
      const next = previous.slice();
      next[index] = {
        ...current,
        needsTokenRefresh: required,
      };
      return next;
    });
  }, [mountedRef]);

  const setUsageRefreshStatus = useCallback((
    accountId: string,
    status: AccountUsageRefreshStatus,
    message: string | null = null,
  ): void => {
    if (!mountedRef.current) {
      return;
    }
    setAccounts((previous) => {
      const index = previous.findIndex((account) => account.id === accountId);
      if (index < 0) {
        return previous;
      }
      const current = previous[index];
      const next = previous.slice();
      next[index] = {
        ...current,
        usageRefreshStatus: status,
        usageRefreshMessage: message,
        usageRefreshUpdatedAtMs: Date.now(),
      };
      return next;
    });
  }, [mountedRef]);

  const resetRefreshErrorFlags = useCallback((): void => {
    resetAccountRefreshErrorFlags(accountsPollErrorReportedRef, fastRefreshErrorReportedRef);
  }, []);

  const applyTrafficFrame = useCallback((frame: AccountTrafficFrame): void => {
    if (!mountedRef.current) {
      return;
    }
    setAccounts((previous) => applyTrafficFrameToAccounts(previous, frame, pendingTrafficByAccountRef.current));
  }, [mountedRef]);

  const applyTrafficSnapshot = useCallback((snapshot: RuntimeAccountTraffic[]): void => {
    for (const account of snapshot) {
      applyTrafficFrame(runtimeTrafficRecordToFrame(account));
    }
  }, [applyTrafficFrame]);

  const refreshAccountsFromNative = useDedupedRefresh(async (): Promise<RuntimeAccount[]> => {
    const runtimeAccounts = await service.listAccountsRequest();
    if (!mountedRef.current) {
      return runtimeAccounts;
    }
    setAccounts((previous) =>
      mapRuntimeAccountsWithTraffic(runtimeAccounts, previous, pendingTrafficByAccountRef.current),
    );
    resetRefreshErrorFlags();

    return runtimeAccounts;
  });

  const refreshAccountTrafficSnapshot = useDedupedRefresh(async (): Promise<void> => {
    const snapshot = await service.listAccountTrafficRequest();
    if (!mountedRef.current) {
      return;
    }
    applyTrafficSnapshot(snapshot);
  });

  useEffect(() => {
    const handle = setInterval(() => {
      void refreshAccountsFromNative().catch(() => {
        reportAccountPollingFailureOnce(accountsPollErrorReportedRef, options.pushRuntimeEvent);
      });
    }, accountsRefreshMs);

    return () => {
      clearInterval(handle);
    };
  }, [accountsRefreshMs, refreshAccountsFromNative]);

  const trafficStreamState = useTrafficStream({
    runtimeBind: options.runtimeBind,
    enableWebSocket,
    trafficReconnectMs,
    trafficClockTickMs,
    trafficSnapshotPollMs,
    trafficActiveWindowMs,
    onTrafficFrame: applyTrafficFrame,
    refreshSnapshot: refreshAccountTrafficSnapshot,
    reportPollingError: (message) => options.pushRuntimeEvent(message, 'warn'),
  });

  const triggerFastAccountRefresh = useCallback(() => {
    if (!tryStartFastAccountRefresh(lastFastAccountRefreshMsRef, Date.now(), fastRefreshCooldownMs)) {
      return;
    }
    void refreshAccountsFromNative().catch(() => {
      reportFastAccountRefreshFailureOnce(fastRefreshErrorReportedRef, options.pushRuntimeEvent);
    });
  }, [fastRefreshCooldownMs, refreshAccountsFromNative]);

  const refreshUsageTelemetryAfterAccountAddAction = useCallback(async (accountId: string, accountName: string): Promise<void> => {
    await refreshUsageTelemetryAfterAccountAdd(
      {
        service,
        applyRuntimeAccountPatch,
        pushRuntimeEvent: options.pushRuntimeEvent,
      },
      accountId,
      accountName,
    );
  }, [applyRuntimeAccountPatch, options.pushRuntimeEvent, service]);

  const toggleAccountPin = useCallback(async (accountId: string, nextPinned: boolean): Promise<void> => {
    await toggleAccountPinAction(
      {
        service,
        accountsRef,
        refreshAccountsFromNative,
        resetRefreshErrorFlags,
        pushRuntimeEvent: options.pushRuntimeEvent,
      },
      accountId,
      nextPinned,
    );
  }, [options.pushRuntimeEvent, refreshAccountsFromNative, resetRefreshErrorFlags, service]);

  const pauseAccount = useCallback(async (accountId: string): Promise<void> => {
    await pauseAccountAction(
      {
        service,
        accountsRef,
        refreshAccountsFromNative,
        resetRefreshErrorFlags,
        pushRuntimeEvent: options.pushRuntimeEvent,
      },
      accountId,
    );
  }, [options.pushRuntimeEvent, refreshAccountsFromNative, resetRefreshErrorFlags, service]);

  const reactivateAccount = useCallback(async (accountId: string): Promise<void> => {
    await reactivateAccountAction(
      {
        service,
        accountsRef,
        refreshAccountsFromNative,
        resetRefreshErrorFlags,
        pushRuntimeEvent: options.pushRuntimeEvent,
      },
      accountId,
    );
  }, [options.pushRuntimeEvent, refreshAccountsFromNative, resetRefreshErrorFlags, service]);

  const deleteAccount = useCallback(async (accountId: string): Promise<boolean> => {
    return deleteAccountAction(
      {
        service,
        accountsRef,
        refreshAccountsFromNative,
        resetRefreshErrorFlags,
        pushRuntimeEvent: options.pushRuntimeEvent,
      },
      accountId,
    );
  }, [options.pushRuntimeEvent, refreshAccountsFromNative, resetRefreshErrorFlags, service]);

  const refreshAccountTelemetry = useCallback(async (accountId: string): Promise<void> => {
    await refreshAccountTelemetryAction(
      {
        service,
        accountsRef,
        refreshAccountsFromNative,
        resetRefreshErrorFlags,
        pushRuntimeEvent: options.pushRuntimeEvent,
        refreshingAccountTelemetryId,
        setRefreshingAccountTelemetryId,
        setTokenRefreshRequired,
        setUsageRefreshStatus,
        isMounted: () => mountedRef.current,
      },
      accountId,
    );
  }, [
    mountedRef,
    options.pushRuntimeEvent,
    refreshAccountsFromNative,
    refreshingAccountTelemetryId,
    resetRefreshErrorFlags,
    service,
    setTokenRefreshRequired,
    setUsageRefreshStatus,
  ]);

  const refreshAllAccountsTelemetry = useCallback(async (): Promise<void> => {
    await refreshAllAccountsTelemetryAction(
      {
        service,
        accountsRef,
        refreshAccountsFromNative,
        resetRefreshErrorFlags,
        pushRuntimeEvent: options.pushRuntimeEvent,
        isRefreshingAllAccountTelemetry: refreshingAllAccountTelemetry,
        setRefreshingAllAccountTelemetry,
        setTokenRefreshRequired,
        setUsageRefreshStatus,
        isMounted: () => mountedRef.current,
      },
    );
  }, [
    mountedRef,
    options.pushRuntimeEvent,
    refreshAccountsFromNative,
    refreshingAllAccountTelemetry,
    resetRefreshErrorFlags,
    service,
    setTokenRefreshRequired,
    setUsageRefreshStatus,
  ]);

  const refreshAccountToken = useCallback(async (accountId: string): Promise<void> => {
    await refreshAccountTokenAction(
      {
        service,
        accountsRef,
        refreshAccountsFromNative,
        resetRefreshErrorFlags,
        pushRuntimeEvent: options.pushRuntimeEvent,
        refreshingAccountTokenId,
        setRefreshingAccountTokenId,
        setTokenRefreshRequired,
        setUsageRefreshStatus,
        isMounted: () => mountedRef.current,
      },
      accountId,
    );
  }, [
    accountsRef,
    mountedRef,
    options.pushRuntimeEvent,
    refreshAccountsFromNative,
    refreshingAccountTokenId,
    resetRefreshErrorFlags,
    service,
    setTokenRefreshRequired,
    setUsageRefreshStatus,
  ]);

  const runAutoResetTelemetryRefresh = useCallback(async (queued: Array<{ id: string; name: string }>): Promise<void> => {
    if (queued.length === 0) {
      return;
    }

    let refreshedAny = false;
    for (const account of queued) {
      if (!mountedRef.current) {
        return;
      }

      try {
        await service.refreshAccountUsageTelemetryRequest(account.id);
        refreshedAny = true;
        options.pushRuntimeEvent(`usage telemetry auto-refreshed after reset: ${account.name}`, 'success');
      } catch {
        options.pushRuntimeEvent(`usage telemetry auto-refresh failed after reset: ${account.name}`, 'warn');
      } finally {
        autoResetTelemetryInFlightRef.current.delete(account.id);
      }
    }

    if (refreshedAny) {
      void refreshAccountsFromNative().catch(() => {
        reportFastAccountRefreshFailureOnce(fastRefreshErrorReportedRef, options.pushRuntimeEvent);
      });
    }
  }, [mountedRef, options.pushRuntimeEvent, refreshAccountsFromNative, service]);

  useEffect(() => {
    if (refreshingAllAccountTelemetry) {
      return;
    }

    const nowMs = Date.now();
    const queued: Array<{ id: string; name: string }> = [];
    for (const account of accounts) {
      if (!hasExhaustedQuotaReadyForRefresh(account, nowMs)) {
        continue;
      }
      if (autoResetTelemetryInFlightRef.current.has(account.id)) {
        continue;
      }
      const lastAttemptAt = autoResetTelemetryLastAttemptMsRef.current.get(account.id) ?? 0;
      if (nowMs - lastAttemptAt < AUTO_RESET_TELEMETRY_RETRY_MS) {
        continue;
      }

      autoResetTelemetryInFlightRef.current.add(account.id);
      autoResetTelemetryLastAttemptMsRef.current.set(account.id, nowMs);
      queued.push({ id: account.id, name: account.name });
      if (queued.length >= AUTO_RESET_TELEMETRY_BATCH_SIZE) {
        break;
      }
    }

    if (queued.length > 0) {
      void runAutoResetTelemetryRefresh(queued);
    }
  }, [accounts, refreshingAllAccountTelemetry, runAutoResetTelemetryRefresh]);

  const isRefreshingAccountTelemetry = useCallback((accountId: string | null | undefined): boolean => {
    if (!accountId) {
      return false;
    }
    return refreshingAccountTelemetryId === accountId;
  }, [refreshingAccountTelemetryId]);

  const isRefreshingAccountToken = useCallback((accountId: string | null | undefined): boolean => {
    if (!accountId) {
      return false;
    }
    return refreshingAccountTokenId === accountId;
  }, [refreshingAccountTokenId]);

  return {
    accounts,
    trafficClockMs: trafficStreamState.trafficClockMs,
    trafficActiveWindowMs: trafficStreamState.trafficActiveWindowMs,
    isRefreshingAllAccountTelemetry: refreshingAllAccountTelemetry,
    refreshAccountsFromNative,
    refreshAccountTrafficSnapshot,
    applyTrafficFrame,
    triggerFastAccountRefresh,
    refreshUsageTelemetryAfterAccountAdd: refreshUsageTelemetryAfterAccountAddAction,
    toggleAccountPin,
    pauseAccount,
    reactivateAccount,
    deleteAccount,
    refreshAccountTelemetry,
    refreshAllAccountsTelemetry,
    refreshAccountToken,
    isRefreshingAccountTelemetry,
    isRefreshingAccountToken,
  };
}
