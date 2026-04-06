import type { RouteRow } from '../../shared/types';

export interface AccountUsage24h {
  requests: number;
  tokens: number;
  costUsd: number;
  failovers: number;
}

function routeRequestedAtMs(row: RouteRow): number | null {
  const value = typeof row.requestedAt === 'string' ? row.requestedAt.trim() : '';
  if (value.length === 0) {
    return null;
  }
  const parsed = new Date(value.includes('T') ? value : `${value.replace(' ', 'T')}Z`);
  const ms = parsed.getTime();
  return Number.isFinite(ms) ? ms : null;
}

export function deriveAccountUsage24h(rows: RouteRow[], accountId: string | null): AccountUsage24h {
  if (!accountId) {
    return { requests: 0, tokens: 0, costUsd: 0, failovers: 0 };
  }

  const now = Date.now();
  const windowStart = now - 24 * 60 * 60 * 1000;
  let requests = 0;
  let tokens = 0;
  let costUsd = 0;
  let failovers = 0;

  for (const row of rows) {
    if (row.accountId !== accountId) {
      continue;
    }
    const timestampMs = routeRequestedAtMs(row);
    if (timestampMs !== null && timestampMs < windowStart) {
      continue;
    }

    requests += 1;
    if (Number.isFinite(row.tokens) && row.tokens > 0) {
      tokens += Math.max(0, Math.round(row.tokens));
    }
    if (typeof row.totalCost === 'number' && Number.isFinite(row.totalCost) && row.totalCost > 0) {
      costUsd += row.totalCost;
    }
    const statusCode = typeof row.statusCode === 'number' ? row.statusCode : 0;
    if (row.status === 'error' || (statusCode >= 500 && statusCode < 600)) {
      failovers += 1;
    }
  }

  return {
    requests,
    tokens,
    costUsd: Math.max(0, costUsd),
    failovers,
  };
}
