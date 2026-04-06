import type { Account, AccountState } from './types';

export function mapAccountState(status: string): AccountState {
  if (
    status === 'active' ||
    status === 'paused' ||
    status === 'rate_limited' ||
    status === 'deactivated' ||
    status === 'quota_blocked'
  ) {
    return status;
  }
  return 'deactivated';
}

export function mapRuntimePlan(planType: string | null | undefined): Account['plan'] {
  const normalized = typeof planType === 'string' ? planType.trim().toLowerCase() : '';
  if (normalized.includes('enterprise') || normalized.includes('business') || normalized.includes('team')) {
    return 'enterprise';
  }
  if (normalized.includes('free') || normalized.includes('guest')) {
    return 'free';
  }
  if (normalized.includes('plus') || normalized.includes('pro') || normalized.includes('premium') || normalized.includes('paid')) {
    return 'plus';
  }
  return 'free';
}

export function runtimeNumber(value: unknown): number | null {
  if (typeof value !== 'number' || !Number.isFinite(value)) {
    return null;
  }
  return value;
}

export function formatSessionTimestamp(ms: number | null | undefined): string {
  if (typeof ms !== 'number' || !Number.isFinite(ms) || ms <= 0) {
    return '—';
  }
  const date = new Date(ms);
  if (Number.isNaN(date.getTime())) {
    return '—';
  }
  const year = date.getFullYear();
  const month = String(date.getMonth() + 1).padStart(2, '0');
  const day = String(date.getDate()).padStart(2, '0');
  const hours = String(date.getHours()).padStart(2, '0');
  const minutes = String(date.getMinutes()).padStart(2, '0');
  const seconds = String(date.getSeconds()).padStart(2, '0');
  return `${year}-${month}-${day} ${hours}:${minutes}:${seconds}`;
}
