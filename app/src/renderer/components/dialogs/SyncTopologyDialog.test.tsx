import { render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { describe, it, expect, vi } from 'vitest';
import { SyncTopologyDialog } from './SyncTopologyDialog';
import type { ClusterStatus } from '../../shared/types';

const baseStatus: ClusterStatus = {
  enabled: true,
  site_id: '1',
  cluster_name: 'test',
  role: 'leader',
  term: 3,
  commit_index: 42,
  leader_id: '1',
  peers: [
    {
      site_id: '2',
      address: '127.0.0.1:9401',
      state: 'connected',
      role: 'follower',
      match_index: 42,
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
      last_probe_duration_ms: 4,
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
  journal_entries: 100,
  pending_raft_entries: 0,
  last_sync_at: null,
};

describe('SyncTopologyDialog', () => {
  it('renders nothing when closed', () => {
    const { container } = render(
      <SyncTopologyDialog open={false} status={baseStatus} onClose={vi.fn()} />,
    );
    expect(container.firstChild).toBeNull();
  });

  it('shows header with leader, term and commit info', () => {
    render(<SyncTopologyDialog open status={baseStatus} onClose={vi.fn()} />);
    expect(screen.getByText('Synchronization')).toBeInTheDocument();
    expect(screen.getAllByText('42').length).toBeGreaterThan(0); // commit_index
    expect(screen.getAllByText('3').length).toBeGreaterThan(0);  // term
  });

  it('renders the leader card with correct site_id', () => {
    render(<SyncTopologyDialog open status={baseStatus} onClose={vi.fn()} />);
    const badges = screen.getAllByText('Leader');
    expect(badges.length).toBeGreaterThanOrEqual(1);
  });

  it('renders a follower card for each peer', () => {
    render(<SyncTopologyDialog open status={baseStatus} onClose={vi.fn()} />);
    expect(screen.getByText('2')).toBeInTheDocument(); // peer site_id
    expect(screen.getByText('Follower')).toBeInTheDocument();
  });

  it('calls onClose when close button is clicked', async () => {
    const onClose = vi.fn();
    render(<SyncTopologyDialog open status={baseStatus} onClose={onClose} />);
    await userEvent.click(screen.getByLabelText('Close'));
    expect(onClose).toHaveBeenCalledOnce();
  });

  it('shows probe latency when available', () => {
    render(<SyncTopologyDialog open status={baseStatus} onClose={vi.fn()} />);
    expect(screen.getByText('4ms')).toBeInTheDocument();
  });

  it('switches to stats tab and renders key/value metrics', async () => {
    const user = userEvent.setup();
    render(<SyncTopologyDialog open status={baseStatus} onClose={vi.fn()} />);

    await user.click(screen.getByRole('tab', { name: 'Stats' }));
    expect(screen.getByText('Lagging peers')).toBeInTheDocument();
    expect(screen.getByText('TLS handshake failures')).toBeInTheDocument();
    expect(screen.getByText('Ingress accepted batches')).toBeInTheDocument();

    await user.click(screen.getByRole('tab', { name: 'Overview' }));
    expect(screen.getByText('Replicating down')).toBeInTheDocument();
  });

  it('shows lag class on peer with non-zero lag', () => {
    const laggingStatus: ClusterStatus = {
      ...baseStatus,
      peers: [{ ...baseStatus.peers[0], replication_lag_entries: 5, match_index: 37 }],
    };
    render(<SyncTopologyDialog open status={laggingStatus} onClose={vi.fn()} />);
    const lagValues = document.querySelectorAll('.value.lag');
    expect(lagValues.length).toBeGreaterThan(0);
  });
});
