import { formatSessionTimestamp } from '../../shared/coerce';
import type { RuntimeStickySession, SessionKind, StickySession } from '../../shared/types';

function inferSessionKind(sessionKey: string): SessionKind {
  const key = sessionKey.trim().toLowerCase();
  if (key.startsWith('ak_') || key.includes('prompt_cache') || key.includes('prompt-cache')) {
    return 'prompt_cache';
  }
  if (
    key.startsWith('turn_') ||
    key.startsWith('http_turn_') ||
    key.startsWith('codex-thread-') ||
    key.startsWith('codex_session') ||
    key.includes('session')
  ) {
    return 'codex_session';
  }
  return 'sticky_thread';
}

function coerceSessionKind(value: unknown): SessionKind | null {
  if (value === 'codex_session' || value === 'sticky_thread' || value === 'prompt_cache') {
    return value;
  }
  return null;
}

export function clampSessionsOffset(offset: number, totalCount: number, pageSize: number): number {
  if (totalCount <= 0) {
    return 0;
  }
  const lastOffset = Math.floor((totalCount - 1) / pageSize) * pageSize;
  return Math.max(0, Math.min(offset, lastOffset));
}

export function mapRuntimeStickySessionToUiSession(record: RuntimeStickySession, generatedAtMs: number): StickySession {
  const sessionKey = typeof record.sessionKey === 'string' ? record.sessionKey : '';
  const accountId = typeof record.accountId === 'string' ? record.accountId : '';
  const kind = coerceSessionKind(record.kind) ?? inferSessionKind(sessionKey);
  const updatedAtMs = Number.isFinite(record.updatedAtMs) ? Math.trunc(record.updatedAtMs) : 0;
  const expiresAtMs = Number.isFinite(record.expiresAtMs) ? Math.trunc(record.expiresAtMs) : 0;
  const hasExpiry = expiresAtMs > 0;
  return {
    key: sessionKey,
    kind,
    accountId,
    updated: formatSessionTimestamp(updatedAtMs),
    expiry: hasExpiry ? formatSessionTimestamp(expiresAtMs) : null,
    stale: hasExpiry && expiresAtMs <= generatedAtMs,
  };
}
