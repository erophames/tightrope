import { act, renderHook } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { ClusterStatus, DashboardSettings } from '../../shared/types';
import { useClusterSync, type UseClusterSyncOptions } from './useClusterSync';

afterEach(() => {
  vi.useRealTimers();
});

function makeSettings(overrides: Partial<DashboardSettings> = {}): DashboardSettings {
  return {
    theme: 'auto',
    stickyThreadsEnabled: false,
    upstreamStreamTransport: 'default',
    preferEarlierResetAccounts: false,
    routingStrategy: 'weighted_round_robin',
    strictLockPoolContinuations: false,
    lockedRoutingAccountIds: [],
    openaiCacheAffinityMaxAgeSeconds: 300,
    importWithoutOverwrite: false,
    totpRequiredOnLogin: false,
    totpConfigured: false,
    apiKeyAuthEnabled: false,
    routingHeadroomWeightPrimary: 0.35,
    routingHeadroomWeightSecondary: 0.65,
    routingScoreAlpha: 0.3,
    routingScoreBeta: 0.25,
    routingScoreGamma: 0.2,
    routingScoreDelta: 0.2,
    routingScoreZeta: 0.05,
    routingScoreEta: 1,
    routingSuccessRateRho: 2,
    routingPlanModelPricingUsdPerMillion: '',
    syncClusterName: 'default',
    syncSiteId: 1,
    syncPort: 9400,
    syncDiscoveryEnabled: true,
    syncIntervalSeconds: 5,
    syncConflictResolution: 'lww',
    syncJournalRetentionDays: 30,
    syncTlsEnabled: true,
    syncRequireHandshakeAuth: true,
    syncClusterSharedSecret: '',
    syncTlsVerifyPeer: true,
    syncTlsCaCertificatePath: '',
    syncTlsCertificateChainPath: '',
    syncTlsPrivateKeyPath: '',
    syncTlsPinnedPeerCertificateSha256: '',
    syncSchemaVersion: 1,
    syncMinSupportedSchemaVersion: 1,
    syncAllowSchemaDowngrade: false,
    syncPeerProbeEnabled: true,
    syncPeerProbeIntervalMs: 5000,
    syncPeerProbeTimeoutMs: 500,
    syncPeerProbeMaxPerRefresh: 2,
    syncPeerProbeFailClosed: true,
    syncPeerProbeFailClosedFailures: 3,
    ...overrides,
  };
}

function makeClusterStatus(overrides: Partial<ClusterStatus> = {}): ClusterStatus {
  return {
    enabled: false,
    site_id: '',
    cluster_name: '',
    role: 'standalone',
    term: 0,
    commit_index: 0,
    leader_id: null,
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

type ClusterSyncServiceMocks = Pick<
  UseClusterSyncOptions,
  | 'getClusterStatusRequest'
  | 'clusterEnableRequest'
  | 'clusterDisableRequest'
  | 'addPeerRequest'
  | 'removePeerRequest'
  | 'triggerSyncRequest'
  | 'updateSettingsRequest'
>;

function createClusterSyncServiceMocks(overrides: Partial<ClusterSyncServiceMocks> = {}): ClusterSyncServiceMocks {
  return {
    getClusterStatusRequest: vi.fn(async () => makeClusterStatus()),
    clusterEnableRequest: vi.fn(async () => true),
    clusterDisableRequest: vi.fn(async () => true),
    addPeerRequest: vi.fn(async () => true),
    removePeerRequest: vi.fn(async () => true),
    triggerSyncRequest: vi.fn(async () => true),
    updateSettingsRequest: vi.fn(async () => null),
    ...overrides,
  };
}

describe('useClusterSync', () => {
  it('refreshes and normalizes cluster status payloads', async () => {
    const settings = makeSettings();
    const pushRuntimeEvent = vi.fn();
    const service = createClusterSyncServiceMocks({
      getClusterStatusRequest: vi.fn().mockResolvedValue({
        enabled: true,
        site_id: '1',
        cluster_name: 'alpha',
        role: 'leader',
        term: 3,
        commit_index: 9,
        leader_id: '1',
        peers: [{ site_id: '2', address: '10.0.0.2:9400' }],
      }),
    });

    const { result } = renderHook(() =>
      useClusterSync(
        {
          dashboardSettings: settings,
          appliedDashboardSettings: settings,
          hasUnsavedSettingsChanges: false,
          makeSettingsUpdate: () => ({}),
          applyPersistedDashboardSettings: vi.fn(),
          pushRuntimeEvent,
          ...service,
        },
        { clusterPollMs: 60_000 },
      ),
    );

    await act(async () => {
      await result.current.refreshClusterState();
    });

    expect(result.current.clusterStatus.enabled).toBe(true);
    expect(result.current.clusterStatus.peers).toHaveLength(1);
    expect(result.current.clusterStatus.peers[0].state).toBe('disconnected');
    expect(result.current.clusterStatus.peers[0].match_index).toBe(0);
  });

  it('falls back to discovery disabled when sync enable rejects localhost discovery host', async () => {
    const settings = makeSettings({ syncDiscoveryEnabled: true });
    const pushRuntimeEvent = vi.fn();
    const applyPersistedDashboardSettings = vi.fn();
    const makeSettingsUpdate = vi.fn((next: DashboardSettings) => ({
      syncDiscoveryEnabled: next.syncDiscoveryEnabled,
    }));
    const clusterEnable = vi
      .fn()
      .mockRejectedValueOnce(
        new Error(
          'clusterEnable failed: cluster discovery requires a routable host; set TIGHTROPE_HOST or TIGHTROPE_CONNECT_ADDRESS',
        ),
      )
      .mockResolvedValue(true);

    const service = createClusterSyncServiceMocks({
      clusterEnableRequest: clusterEnable,
      getClusterStatusRequest: vi.fn().mockResolvedValue(makeClusterStatus({ enabled: true })),
      updateSettingsRequest: vi.fn(async (update) => ({ ...settings, ...update })),
    });

    const { result } = renderHook(() =>
      useClusterSync(
        {
          dashboardSettings: settings,
          appliedDashboardSettings: settings,
          hasUnsavedSettingsChanges: false,
          makeSettingsUpdate,
          applyPersistedDashboardSettings,
          pushRuntimeEvent,
          ...service,
        },
        { clusterPollMs: 60_000 },
      ),
    );

    await act(async () => {
      await result.current.toggleSyncEnabled();
    });

    expect(clusterEnable).toHaveBeenCalledTimes(2);
    expect(clusterEnable.mock.calls[0][0]).toMatchObject({ discovery_enabled: true });
    expect(clusterEnable.mock.calls[1][0]).toMatchObject({ discovery_enabled: false });
    expect(makeSettingsUpdate).toHaveBeenCalled();
    expect(applyPersistedDashboardSettings).toHaveBeenCalledWith(
      expect.objectContaining({ syncDiscoveryEnabled: false }),
      false,
    );
    expect(pushRuntimeEvent).toHaveBeenCalledWith(
      expect.stringContaining('peer discovery disabled for localhost'),
      'warn',
    );
  });

  it('adds a manual peer and clears draft value', async () => {
    const settings = makeSettings();
    const pushRuntimeEvent = vi.fn();
    const addPeer = vi.fn().mockResolvedValue(true);
    const service = createClusterSyncServiceMocks({
      addPeerRequest: addPeer,
      getClusterStatusRequest: vi.fn().mockResolvedValue(makeClusterStatus({ enabled: true })),
    });

    const { result } = renderHook(() =>
      useClusterSync(
        {
          dashboardSettings: settings,
          appliedDashboardSettings: settings,
          hasUnsavedSettingsChanges: false,
          makeSettingsUpdate: () => ({}),
          applyPersistedDashboardSettings: vi.fn(),
          pushRuntimeEvent,
          ...service,
        },
        { clusterPollMs: 60_000 },
      ),
    );

    act(() => {
      result.current.setManualPeer(' 10.0.0.5:9400 ');
    });

    await act(async () => {
      await result.current.addManualPeer();
    });

    expect(addPeer).toHaveBeenCalledWith('10.0.0.5:9400');
    expect(result.current.manualPeerAddress).toBe('');
    expect(pushRuntimeEvent).toHaveBeenCalledWith('sync peer added 10.0.0.5:9400', 'success');
  });

  it('reports cluster polling error once until recovery', async () => {
    vi.useFakeTimers();
    const settings = makeSettings();
    const pushRuntimeEvent = vi.fn();
    const getClusterStatus = vi.fn().mockRejectedValue(new Error('down'));
    const service = createClusterSyncServiceMocks({
      getClusterStatusRequest: getClusterStatus,
    });

    const { result } = renderHook(() =>
      useClusterSync(
        {
          dashboardSettings: settings,
          appliedDashboardSettings: settings,
          hasUnsavedSettingsChanges: false,
          makeSettingsUpdate: () => ({}),
          applyPersistedDashboardSettings: vi.fn(),
          pushRuntimeEvent,
          ...service,
        },
        { clusterPollMs: 10 },
      ),
    );

    await act(async () => {
      vi.advanceTimersByTime(30);
      await Promise.resolve();
    });

    expect(pushRuntimeEvent).toHaveBeenCalledWith('cluster polling failed; retrying', 'warn');
    expect(pushRuntimeEvent).toHaveBeenCalledTimes(1);

    await act(async () => {
      getClusterStatus.mockResolvedValueOnce(makeClusterStatus({ enabled: true }));
      await result.current.refreshClusterState();
      getClusterStatus.mockRejectedValueOnce(new Error('down'));
    });

    await act(async () => {
      vi.advanceTimersByTime(10);
      await Promise.resolve();
    });

    expect(pushRuntimeEvent).toHaveBeenCalledTimes(2);
    expect(pushRuntimeEvent).toHaveBeenLastCalledWith('cluster polling failed; retrying', 'warn');
  });
});
