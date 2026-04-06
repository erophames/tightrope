import type { Account, RuntimeAccount, RuntimeAccountTraffic } from '../../shared/types';
import { mapAccountState, mapRuntimePlan, runtimeNumber } from '../../shared/coerce';
import type { AccountTrafficFrame } from './useTrafficStream';

export function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => {
    setTimeout(resolve, ms);
  });
}

function withExistingTraffic(mapped: Account, existing: Account | undefined): Account {
  if (!existing) {
    return mapped;
  }
  return {
    ...mapped,
    needsTokenRefresh: existing.needsTokenRefresh === true,
    usageRefreshStatus: existing.usageRefreshStatus ?? 'unknown',
    usageRefreshMessage: existing.usageRefreshMessage ?? null,
    usageRefreshUpdatedAtMs: existing.usageRefreshUpdatedAtMs ?? null,
    trafficUpBytes: existing.trafficUpBytes ?? 0,
    trafficDownBytes: existing.trafficDownBytes ?? 0,
    trafficLastUpAtMs: existing.trafficLastUpAtMs ?? 0,
    trafficLastDownAtMs: existing.trafficLastDownAtMs ?? 0,
  };
}

export function runtimeAccountToUiAccount(record: RuntimeAccount): Account {
  const state = mapAccountState(record.status);
  const load = runtimeNumber(record.loadPercent);
  const quotaPrimary = runtimeNumber(record.quotaPrimaryPercent);
  const quotaSecondary = runtimeNumber(record.quotaSecondaryPercent);
  const quotaPrimaryWindowSeconds = runtimeNumber(record.quotaPrimaryWindowSeconds);
  const quotaSecondaryWindowSeconds = runtimeNumber(record.quotaSecondaryWindowSeconds);
  const quotaPrimaryResetAtMs = runtimeNumber(record.quotaPrimaryResetAtMs);
  const quotaSecondaryResetAtMs = runtimeNumber(record.quotaSecondaryResetAtMs);
  const hasPrimaryQuota = quotaPrimary !== null;
  const hasSecondaryQuota = quotaSecondary !== null;
  const inflight = runtimeNumber(record.inflight);
  const latency = runtimeNumber(record.latencyMs);
  const errorEwma = runtimeNumber(record.errorEwma);
  const stickyHit = runtimeNumber(record.stickyHitPercent);
  const routed24h = runtimeNumber(record.requests24h);
  const failovers = runtimeNumber(record.failovers24h);
  const costNorm = runtimeNumber(record.costNorm);
  const telemetryBacked = [
    load,
    quotaPrimary,
    quotaSecondary,
    inflight,
    latency,
    errorEwma,
    stickyHit,
    routed24h,
    failovers,
    costNorm,
  ].some((value) => value !== null);

  const capability = state === 'active';
  const cooldown = state !== 'active';
  const resolvedErrorEwma = errorEwma ?? 0;
  const health: Account['health'] = state === 'active' ? (resolvedErrorEwma < 0.15 ? 'healthy' : 'strained') : 'strained';
  const plan = mapRuntimePlan(record.planType);

  return {
    id: record.accountId,
    name: record.email,
    pinned: record.routingPinned === true,
    plan,
    health,
    state,
    inflight: inflight ?? 0,
    load: load ?? 0,
    latency: latency ?? 0,
    errorEwma: resolvedErrorEwma,
    cooldown,
    capability,
    costNorm: costNorm ?? 0,
    routed24h: routed24h ?? 0,
    stickyHit: stickyHit ?? 0,
    quotaPrimary: quotaPrimary ?? 0,
    quotaSecondary: quotaSecondary ?? 0,
    hasPrimaryQuota,
    hasSecondaryQuota,
    quotaPrimaryWindowSeconds,
    quotaSecondaryWindowSeconds,
    quotaPrimaryResetAtMs,
    quotaSecondaryResetAtMs,
    failovers: failovers ?? 0,
    note: record.provider || 'openai',
    telemetryBacked,
    needsTokenRefresh: false,
    usageRefreshStatus: 'unknown',
    usageRefreshMessage: null,
    usageRefreshUpdatedAtMs: null,
    trafficUpBytes: 0,
    trafficDownBytes: 0,
    trafficLastUpAtMs: 0,
    trafficLastDownAtMs: 0,
  };
}

export function patchRuntimeAccountKeepingTraffic(previous: Account[], record: RuntimeAccount): Account[] {
  const patch = runtimeAccountToUiAccount(record);
  const index = previous.findIndex((account) => account.id === patch.id);
  if (index < 0) {
    return previous;
  }

  const next = previous.slice();
  next[index] = withExistingTraffic(patch, next[index]);
  return next;
}

export function applyTrafficFrameToAccounts(
  previous: Account[],
  frame: AccountTrafficFrame,
  pendingTrafficByAccount: Map<string, AccountTrafficFrame>,
): Account[] {
  const index = previous.findIndex((account) => account.id === frame.accountId);
  if (index < 0) {
    pendingTrafficByAccount.set(frame.accountId, frame);
    return previous;
  }

  const current = previous[index];
  const currentUpBytes = current.trafficUpBytes ?? 0;
  const currentDownBytes = current.trafficDownBytes ?? 0;
  const upstreamIncreased = frame.upBytes > currentUpBytes;
  const downstreamIncreased = frame.downBytes > currentDownBytes;
  const currentLastUpAtMs = current.trafficLastUpAtMs ?? 0;
  const currentLastDownAtMs = current.trafficLastDownAtMs ?? 0;
  const now = Date.now();
  const nextLastUpAtMs = upstreamIncreased ? now : currentLastUpAtMs;
  const nextLastDownAtMs = downstreamIncreased ? now : currentLastDownAtMs;
  if (
    currentUpBytes === frame.upBytes &&
    currentDownBytes === frame.downBytes &&
    currentLastUpAtMs === nextLastUpAtMs &&
    currentLastDownAtMs === nextLastDownAtMs
  ) {
    return previous;
  }

  const next = previous.slice();
  next[index] = {
    ...current,
    trafficUpBytes: frame.upBytes,
    trafficDownBytes: frame.downBytes,
    trafficLastUpAtMs: nextLastUpAtMs,
    trafficLastDownAtMs: nextLastDownAtMs,
  };
  return next;
}

export function mapRuntimeAccountsWithTraffic(
  runtimeAccounts: RuntimeAccount[],
  previousAccounts: Account[],
  pendingTrafficByAccount: Map<string, AccountTrafficFrame>,
): Account[] {
  const previousById = new Map(previousAccounts.map((account) => [account.id, account]));
  const next = runtimeAccounts.map((record) => {
    const mapped = runtimeAccountToUiAccount(record);
    return withExistingTraffic(mapped, previousById.get(mapped.id));
  });

  if (pendingTrafficByAccount.size > 0) {
    for (let i = 0; i < next.length; i += 1) {
      const pending = pendingTrafficByAccount.get(next[i].id);
      if (!pending) {
        continue;
      }
      next[i] = {
        ...next[i],
        trafficUpBytes: pending.upBytes,
        trafficDownBytes: pending.downBytes,
        trafficLastUpAtMs: pending.lastUpAtMs,
        trafficLastDownAtMs: pending.lastDownAtMs,
      };
      pendingTrafficByAccount.delete(next[i].id);
    }
  }

  return next;
}

export function runtimeTrafficRecordToFrame(record: RuntimeAccountTraffic): AccountTrafficFrame {
  return {
    accountId: record.accountId,
    upBytes: record.upBytes,
    downBytes: record.downBytes,
    lastUpAtMs: record.lastUpAtMs,
    lastDownAtMs: record.lastDownAtMs,
  };
}
