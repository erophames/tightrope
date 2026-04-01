import { describe, expect, test } from 'vitest';
import { accountsSeed, routingModesSeed, scoringModelSeed, stickySessionsSeed } from '../data/seed';
import type { ClusterStatus } from '../shared/types';
import { deriveMetrics, filteredRows, generateDeviceCode, paginateSessions, shouldScheduleAutoSync } from './logic';

describe('deriveMetrics', () => {
  test('marks blocked account with infinite score', () => {
    const accounts = accountsSeed.map((account) =>
      account.id === 'acc-overflow' ? { ...account, state: 'paused' as const } : { ...account },
    );

    const metrics = deriveMetrics(accounts, scoringModelSeed, 'power_of_two_choices', 0, routingModesSeed);

    expect(metrics.get('acc-overflow')?.capability).toBe(false);
    expect(metrics.get('acc-overflow')?.score).toBe(Infinity);
  });
});

describe('filteredRows', () => {
  test('filters by selected account and query', () => {
    const rows = [
      {
        time: '01:19:44',
        id: 'req_1',
        model: 'gpt-5.4',
        accountId: 'acc-alice',
        tokens: 1280,
        latency: 176,
        status: 'ok' as const,
        protocol: 'SSE' as const,
        sessionId: 'sess_1',
        sticky: true,
      },
      {
        time: '01:19:43',
        id: 'req_2',
        model: 'gpt-5.3-codex',
        accountId: 'acc-research',
        tokens: 940,
        latency: 248,
        status: 'ok' as const,
        protocol: 'WS' as const,
        sessionId: 'sess_2',
        sticky: false,
      },
    ];

    const results = filteredRows(rows, accountsSeed, 'acc-research', 'codex');
    expect(results).toHaveLength(1);
    expect(results[0]?.id).toBe('req_2');
  });
});

describe('paginateSessions', () => {
  test('returns paged sessions and stale count', () => {
    const view = paginateSessions(stickySessionsSeed, 'prompt_cache', 0, 2);
    expect(view.filtered.every((entry) => entry.kind === 'prompt_cache')).toBe(true);
    expect(view.paged).toHaveLength(2);
    expect(view.staleTotal).toBeGreaterThan(0);
  });
});

describe('generateDeviceCode', () => {
  test('is deterministic for a fixed seed and follows format', () => {
    const code = generateDeviceCode(123);
    expect(code).toBe(generateDeviceCode(123));
    expect(code).toMatch(/^[A-Z]{4}-[A-Z]{4}$/);
  });
});

describe('shouldScheduleAutoSync', () => {
  function status(overrides: Partial<ClusterStatus> = {}): ClusterStatus {
    return {
      enabled: true,
      site_id: '1',
      cluster_name: 'alpha',
      role: 'leader',
      term: 1,
      commit_index: 0,
      leader_id: '1',
      peers: [],
      replication_lagging_peers: 0,
      replication_lag_total_entries: 0,
      replication_lag_max_entries: 0,
      replication_lag_avg_entries: 0,
      replication_lag_ewma_entries: 0,
      replication_lag_ewma_samples: 0,
      replication_lag_alert_threshold_entries: 0,
      replication_lag_alert_sustained_refreshes: 0,
      replication_lag_alert_streak: 0,
      replication_lag_alert_active: false,
      replication_lag_last_alert_at: null,
      ingress_socket_accept_failures: 0,
      ingress_socket_accepted_connections: 0,
      ingress_socket_completed_connections: 0,
      ingress_socket_failed_connections: 0,
      ingress_socket_active_connections: 0,
      ingress_socket_peak_active_connections: 0,
      ingress_socket_tls_handshake_failures: 0,
      ingress_socket_read_failures: 0,
      ingress_socket_apply_failures: 0,
      ingress_socket_handshake_ack_failures: 0,
      ingress_socket_bytes_read: 0,
      ingress_socket_total_connection_duration_ms: 0,
      ingress_socket_last_connection_duration_ms: 0,
      ingress_socket_max_connection_duration_ms: 0,
      ingress_socket_connection_duration_ewma_ms: 0,
      ingress_socket_connection_duration_le_10ms: 0,
      ingress_socket_connection_duration_le_50ms: 0,
      ingress_socket_connection_duration_le_250ms: 0,
      ingress_socket_connection_duration_le_1000ms: 0,
      ingress_socket_connection_duration_gt_1000ms: 0,
      ingress_socket_max_buffered_bytes: 0,
      ingress_socket_max_queued_frames: 0,
      ingress_socket_max_queued_payload_bytes: 0,
      ingress_socket_paused_read_cycles: 0,
      ingress_socket_paused_read_sleep_ms: 0,
      ingress_socket_last_connection_at: null,
      ingress_socket_last_failure_at: null,
      ingress_socket_last_failure_error: null,
      journal_entries: 0,
      pending_raft_entries: 0,
      last_sync_at: null,
      ...overrides,
    };
  }

  test('returns false when there are no connected peers', () => {
    expect(shouldScheduleAutoSync(status(), 5)).toBe(false);
  });

  test('returns true when at least one peer is connected', () => {
    expect(
      shouldScheduleAutoSync(
        status({
          peers: [
            {
              site_id: '2',
              address: '10.0.0.2:9400',
              state: 'connected',
              role: 'follower',
              match_index: 0,
              replication_lag_entries: 0,
              consecutive_heartbeat_failures: 0,
              consecutive_probe_failures: 0,
              ingress_accepted_batches: 0,
              ingress_rejected_batches: 0,
              ingress_accepted_wire_bytes: 0,
              ingress_rejected_wire_bytes: 0,
              ingress_rejected_batch_too_large: 0,
              ingress_rejected_backpressure: 0,
              ingress_rejected_inflight_wire_budget: 0,
              ingress_rejected_handshake_auth: 0,
              ingress_rejected_handshake_schema: 0,
              ingress_rejected_invalid_wire_batch: 0,
              ingress_rejected_entry_limit: 0,
              ingress_rejected_rate_limit: 0,
              ingress_rejected_apply_batch: 0,
              ingress_rejected_ingress_protocol: 0,
              ingress_last_wire_batch_bytes: 0,
              ingress_total_apply_duration_ms: 0,
              ingress_last_apply_duration_ms: 0,
              ingress_max_apply_duration_ms: 0,
              ingress_apply_duration_ewma_ms: 0,
              ingress_apply_duration_samples: 0,
              ingress_total_replication_latency_ms: 0,
              ingress_last_replication_latency_ms: 0,
              ingress_max_replication_latency_ms: 0,
              ingress_replication_latency_ewma_ms: 0,
              ingress_replication_latency_samples: 0,
              ingress_inflight_wire_batches: 0,
              ingress_inflight_wire_batches_peak: 0,
              ingress_inflight_wire_bytes: 0,
              ingress_inflight_wire_bytes_peak: 0,
              last_heartbeat_at: null,
              last_probe_at: null,
              last_probe_duration_ms: null,
              last_probe_error: null,
              last_ingress_rejection_at: null,
              last_ingress_rejection_reason: null,
              last_ingress_rejection_error: null,
              discovered_via: 'manual',
            },
          ],
        }),
        5,
      ),
    ).toBe(true);
  });

  test('returns false when disabled or interval is non-positive', () => {
    expect(shouldScheduleAutoSync(status({ enabled: false }), 5)).toBe(false);
    expect(
      shouldScheduleAutoSync(
        status({
          peers: [
            {
              site_id: '2',
              address: '10.0.0.2:9400',
              state: 'connected',
              role: 'follower',
              match_index: 0,
              replication_lag_entries: 0,
              consecutive_heartbeat_failures: 0,
              consecutive_probe_failures: 0,
              ingress_accepted_batches: 0,
              ingress_rejected_batches: 0,
              ingress_accepted_wire_bytes: 0,
              ingress_rejected_wire_bytes: 0,
              ingress_rejected_batch_too_large: 0,
              ingress_rejected_backpressure: 0,
              ingress_rejected_inflight_wire_budget: 0,
              ingress_rejected_handshake_auth: 0,
              ingress_rejected_handshake_schema: 0,
              ingress_rejected_invalid_wire_batch: 0,
              ingress_rejected_entry_limit: 0,
              ingress_rejected_rate_limit: 0,
              ingress_rejected_apply_batch: 0,
              ingress_rejected_ingress_protocol: 0,
              ingress_last_wire_batch_bytes: 0,
              ingress_total_apply_duration_ms: 0,
              ingress_last_apply_duration_ms: 0,
              ingress_max_apply_duration_ms: 0,
              ingress_apply_duration_ewma_ms: 0,
              ingress_apply_duration_samples: 0,
              ingress_total_replication_latency_ms: 0,
              ingress_last_replication_latency_ms: 0,
              ingress_max_replication_latency_ms: 0,
              ingress_replication_latency_ewma_ms: 0,
              ingress_replication_latency_samples: 0,
              ingress_inflight_wire_batches: 0,
              ingress_inflight_wire_batches_peak: 0,
              ingress_inflight_wire_bytes: 0,
              ingress_inflight_wire_bytes_peak: 0,
              last_heartbeat_at: null,
              last_probe_at: null,
              last_probe_duration_ms: null,
              last_probe_error: null,
              last_ingress_rejection_at: null,
              last_ingress_rejection_reason: null,
              last_ingress_rejection_error: null,
              discovered_via: 'manual',
            },
          ],
        }),
        0,
      ),
    ).toBe(false);
  });
});
