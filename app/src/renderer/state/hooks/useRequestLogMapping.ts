import { runtimeNumber } from '../../shared/coerce';
import type { RouteRow, RuntimeRequestLog } from '../../shared/types';
import { nowStamp } from '../logic';

function mapTransportProtocol(value: string | null | undefined): RouteRow['protocol'] {
  const normalized = typeof value === 'string' ? value.trim().toLowerCase() : '';
  if (normalized === 'websocket' || normalized === 'ws') {
    return 'WS';
  }
  if (normalized === 'compact') {
    return 'Compact';
  }
  if (normalized === 'transcribe') {
    return 'Transcribe';
  }
  return 'SSE';
}

function mapStatusBadge(statusCode: number, errorCode: string | null | undefined): RouteRow['status'] {
  if (statusCode >= 500) {
    return 'error';
  }
  if (statusCode >= 400 || (typeof errorCode === 'string' && errorCode.trim() !== '')) {
    return 'warn';
  }
  return 'ok';
}

function routeTimeLabel(requestedAt: string): string {
  const trimmed = requestedAt.trim();
  const date = new Date(trimmed.includes('T') ? trimmed : `${trimmed.replace(' ', 'T')}Z`);
  if (!Number.isNaN(date.getTime())) {
    return date.toTimeString().slice(0, 8);
  }
  if (trimmed.length >= 8) {
    return trimmed.slice(-8);
  }
  return nowStamp();
}

export function runtimeRequestLogToRouteRow(log: RuntimeRequestLog): RouteRow {
  const accountId = typeof log.accountId === 'string' ? log.accountId : '';
  const statusCode = Number.isFinite(log.statusCode) ? Math.trunc(log.statusCode) : 0;
  const model = typeof log.model === 'string' && log.model.trim() !== '' ? log.model.trim() : '—';
  const path = typeof log.path === 'string' ? log.path : '';
  const method = typeof log.method === 'string' ? log.method : '';
  const requestedAt = typeof log.requestedAt === 'string' ? log.requestedAt : '';
  const errorCode = typeof log.errorCode === 'string' ? log.errorCode : null;
  const transport = typeof log.transport === 'string' ? log.transport : null;
  const totalCost = Number.isFinite(log.totalCost) ? log.totalCost : 0;
  const routingStrategy = typeof log.routingStrategy === 'string' ? log.routingStrategy : null;
  const routingScore = Number.isFinite(log.routingScore) ? log.routingScore : null;
  const sticky = log.sticky === true;
  const latencyValue = runtimeNumber(log.latencyMs);
  const tokensValue = runtimeNumber(log.totalTokens);
  const latency = latencyValue === null ? 0 : Math.max(0, Math.trunc(latencyValue));
  const tokens = tokensValue === null ? 0 : Math.max(0, Math.trunc(tokensValue));

  return {
    id: `log_${log.id}`,
    time: routeTimeLabel(requestedAt),
    model,
    accountId,
    tokens,
    latency,
    status: mapStatusBadge(statusCode, errorCode),
    protocol: mapTransportProtocol(transport),
    sessionId: path,
    sticky,
    method,
    path,
    statusCode,
    errorCode,
    requestedAt,
    totalCost,
    routingStrategy,
    routingScore,
  };
}

export function shouldTriggerImmediateAccountsRefresh(rows: RouteRow[]): boolean {
  return rows.some((row) => {
    if (!row.accountId) {
      return false;
    }
    const statusCode = typeof row.statusCode === 'number' ? row.statusCode : 0;
    const errorCode = typeof row.errorCode === 'string' ? row.errorCode.trim().toLowerCase() : '';
    if (statusCode === 429) {
      return true;
    }
    if (statusCode === 401 && errorCode === 'account_deactivated') {
      return true;
    }
    if (!errorCode) {
      return false;
    }
    return errorCode.includes('rate_limit') || errorCode.includes('quota');
  });
}
