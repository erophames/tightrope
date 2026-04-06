import { describe, expect, it } from 'vitest';
import type { RuntimeStickySession } from '../../shared/types';
import { mapRuntimeStickySessionToUiSession } from './useSessionsMapping';

describe('mapRuntimeStickySessionToUiSession', () => {
  it('prefers server-provided kind when valid', () => {
    const record: RuntimeStickySession = {
      sessionKey: 'ak_server_kind',
      accountId: 'acc_1',
      kind: 'codex_session',
      updatedAtMs: 1_710_000_000_000,
      expiresAtMs: 1_710_000_060_000,
    };

    const mapped = mapRuntimeStickySessionToUiSession(record, 1_710_000_010_000);
    expect(mapped.kind).toBe('codex_session');
  });

  it('falls back to key inference when kind is missing or invalid', () => {
    const missingKind: RuntimeStickySession = {
      sessionKey: 'turn_abc',
      accountId: 'acc_2',
      updatedAtMs: 1_710_000_000_000,
      expiresAtMs: 0,
    };
    const invalidKind = {
      sessionKey: 'ak_prompt_key',
      accountId: 'acc_3',
      kind: 'invalid_kind',
      updatedAtMs: 1_710_000_000_000,
      expiresAtMs: 0,
    } as unknown as RuntimeStickySession;

    expect(mapRuntimeStickySessionToUiSession(missingKind, Date.now()).kind).toBe('codex_session');
    expect(mapRuntimeStickySessionToUiSession(invalidKind, Date.now()).kind).toBe('prompt_cache');
  });
});
