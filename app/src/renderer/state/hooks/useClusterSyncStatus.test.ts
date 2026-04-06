import { describe, expect, test } from 'vitest';
import { emptyClusterStatus, normalizeClusterStatus } from './useClusterSyncStatus';

describe('useClusterSyncStatus', () => {
  test('normalizeClusterStatus falls back to empty shape for invalid payload', () => {
    const normalized = normalizeClusterStatus(null);

    expect(normalized).toEqual(emptyClusterStatus);
  });

  test('normalizeClusterStatus hydrates peer defaults from partial payload', () => {
    const normalized = normalizeClusterStatus({
      enabled: true,
      peers: [{ site_id: '2', address: '10.0.0.2:9400' }],
    });

    expect(normalized.enabled).toBe(true);
    expect(normalized.peers).toHaveLength(1);
    expect(normalized.peers[0].site_id).toBe('2');
    expect(normalized.peers[0].address).toBe('10.0.0.2:9400');
    expect(normalized.peers[0].state).toBe('disconnected');
    expect(normalized.peers[0].match_index).toBe(0);
  });
});
