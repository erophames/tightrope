import { renderHook, act } from '@testing-library/react';
import { createElement, type PropsWithChildren } from 'react';
import { describe, it, expect, vi, beforeEach } from 'vitest';
import { TightropeServiceProvider } from './context';
import { useSyncTopology } from './useSyncTopology';
import type { ClusterStatus, SyncEvent } from '../shared/types';
import type { TightropeService } from '../services/tightrope';
import { makeTestService } from '../test/makeService';

const baseStatus: ClusterStatus = {
  enabled: true,
  site_id: '1',
  cluster_name: 'test',
  role: 'leader',
  term: 1,
  commit_index: 10,
  leader_id: '1',
  peers: [
    {
      site_id: '2',
      address: '127.0.0.1:9401',
      state: 'connected',
      role: 'follower',
      match_index: 10,
      replication_lag_entries: 0,
      consecutive_heartbeat_failures: 0,
      consecutive_probe_failures: 0,
      ingress_accepted_batches: 5,
      ingress_rejected_batches: 0,
      ingress_accepted_wire_bytes: 1024,
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
      ingress_last_wire_batch_bytes: 100,
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
  replication_lagging_peers: 0,
  replication_lag_total_entries: 0,
  replication_lag_max_entries: 0,
  replication_lag_avg_entries: 0,
  replication_lag_ewma_entries: 0,
  replication_lag_ewma_samples: 0,
  replication_lag_alert_threshold_entries: 100,
  replication_lag_alert_sustained_refreshes: 3,
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
  journal_entries: 20,
  pending_raft_entries: 0,
  last_sync_at: null,
};

describe('useSyncTopology', () => {
  let capturedListener: ((event: SyncEvent) => void) | null = null;
  let service: TightropeService;

  function wrapper({ children }: PropsWithChildren) {
    return createElement(TightropeServiceProvider, { service, children });
  }

  beforeEach(() => {
    capturedListener = null;
    service = makeTestService({
      getClusterStatus: vi.fn().mockResolvedValue({ ...baseStatus }),
      onSyncEvent: vi.fn().mockImplementation((listener: (event: SyncEvent) => void) => {
        capturedListener = listener;
        return () => {
          capturedListener = null;
        };
      }),
    });
  });

  it('returns null status when closed', () => {
    const { result } = renderHook(() => useSyncTopology(false), { wrapper });
    expect(result.current.status).toBeNull();
    expect(result.current.lastEventAt).toBeNull();
  });

  it('seeds status from getClusterStatus when opened', async () => {
    const { result } = renderHook(() => useSyncTopology(true), { wrapper });
    await act(async () => {});
    expect(result.current.status).not.toBeNull();
    expect(result.current.status?.commit_index).toBe(10);
  });

  it('patches commit_index on commit_advance event', async () => {
    const { result } = renderHook(() => useSyncTopology(true), { wrapper });
    await act(async () => {});

    act(() => {
      capturedListener!({ type: 'commit_advance', ts: 1000, commit_index: 15, last_applied: 15 });
    });

    expect(result.current.status?.commit_index).toBe(15);
    expect(result.current.lastEventAt).toBe(1000);
  });

  it('patches peer state on peer_state_change event', async () => {
    const { result } = renderHook(() => useSyncTopology(true), { wrapper });
    await act(async () => {});

    act(() => {
      capturedListener!({ type: 'peer_state_change', ts: 2000, site_id: '2', state: 'disconnected', address: '127.0.0.1:9401' });
    });

    expect(result.current.status?.peers[0].state).toBe('disconnected');
  });

  it('patches role on role_change event', async () => {
    const { result } = renderHook(() => useSyncTopology(true), { wrapper });
    await act(async () => {});

    act(() => {
      capturedListener!({ type: 'role_change', ts: 3000, role: 'follower', term: 2, leader_id: '2' });
    });

    expect(result.current.status?.role).toBe('follower');
    expect(result.current.status?.term).toBe(2);
    expect(result.current.status?.leader_id).toBe('2');
  });

  it('increments journal_entries on journal_entry event', async () => {
    const { result } = renderHook(() => useSyncTopology(true), { wrapper });
    await act(async () => {});

    act(() => {
      capturedListener!({ type: 'journal_entry', ts: 4000, seq: 21, table: 'accounts', op: 'insert' });
    });

    expect(result.current.status?.journal_entries).toBe(21);
  });

  it('updates lag_alert fields on lag_alert event', async () => {
    const { result } = renderHook(() => useSyncTopology(true), { wrapper });
    await act(async () => {});

    act(() => {
      capturedListener!({ type: 'lag_alert', ts: 5000, active: true, lagging_peers: 1, max_lag: 500 });
    });

    expect(result.current.status?.replication_lag_alert_active).toBe(true);
    expect(result.current.status?.replication_lagging_peers).toBe(1);
    expect(result.current.status?.replication_lag_max_entries).toBe(500);
  });

  it('unsubscribes when closed', async () => {
    const { result, rerender } = renderHook(({ open }: { open: boolean }) => useSyncTopology(open), {
      initialProps: { open: true },
      wrapper,
    });
    await act(async () => {});
    expect(capturedListener).not.toBeNull();

    rerender({ open: false });
    expect(capturedListener).toBeNull();
    expect(result.current.status).not.toBeNull(); // status preserved after close
  });

  it('polls cluster status while open to reconcile topology snapshots', async () => {
    vi.useFakeTimers();
    try {
      const getClusterStatus = vi.fn().mockResolvedValue({ ...baseStatus });
      service = makeTestService({
        getClusterStatus,
        onSyncEvent: vi.fn().mockImplementation((listener: (event: SyncEvent) => void) => {
          capturedListener = listener;
          return () => {
            capturedListener = null;
          };
        }),
      });

      const { rerender } = renderHook(({ open }: { open: boolean }) => useSyncTopology(open), {
        initialProps: { open: true },
        wrapper,
      });

      await act(async () => {});
      expect(getClusterStatus).toHaveBeenCalledTimes(1);

      await act(async () => {
        vi.advanceTimersByTime(4000);
      });
      await act(async () => {});
      expect(getClusterStatus).toHaveBeenCalledTimes(3);

      rerender({ open: false });
      await act(async () => {
        vi.advanceTimersByTime(4000);
      });
      await act(async () => {});
      expect(getClusterStatus).toHaveBeenCalledTimes(3);
    } finally {
      vi.useRealTimers();
    }
  });
});
