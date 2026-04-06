import { describe, expect, test, vi } from 'vitest';
import { emptyClusterPeerStatus, emptyClusterStatus } from './useClusterSyncStatus';
import { defaultDashboardSettings } from './useSettings';
import {
  CLUSTER_POLLING_FAILED_MESSAGE,
  canScheduleClusterAutoSync,
  reportClusterPollingFailureOnce,
  runClusterAutoSyncTick,
} from './useClusterSyncPolling';

describe('useClusterSyncPolling', () => {
  test('reportClusterPollingFailureOnce reports only once', () => {
    const ref = { current: false };
    const pushRuntimeEvent = vi.fn();

    reportClusterPollingFailureOnce(ref, pushRuntimeEvent);
    reportClusterPollingFailureOnce(ref, pushRuntimeEvent);

    expect(pushRuntimeEvent).toHaveBeenCalledTimes(1);
    expect(pushRuntimeEvent).toHaveBeenCalledWith(CLUSTER_POLLING_FAILED_MESSAGE, 'warn');
  });

  test('canScheduleClusterAutoSync requires enabled cluster with connected peer', () => {
    const settings = { ...defaultDashboardSettings, syncIntervalSeconds: 10 };

    expect(canScheduleClusterAutoSync(emptyClusterStatus, settings)).toBe(false);
    expect(
      canScheduleClusterAutoSync(
        {
          ...emptyClusterStatus,
          enabled: true,
          peers: [{ ...emptyClusterPeerStatus, site_id: '2', address: '10.0.0.2:9400', state: 'disconnected' }],
        },
        settings,
      ),
    ).toBe(false);
    expect(
      canScheduleClusterAutoSync(
        {
          ...emptyClusterStatus,
          enabled: true,
          peers: [{ ...emptyClusterPeerStatus, site_id: '2', address: '10.0.0.2:9400', state: 'connected' }],
        },
        settings,
      ),
    ).toBe(true);
  });

  test('runClusterAutoSyncTick refreshes only when trigger succeeds', async () => {
    const pushRuntimeEvent = vi.fn();
    const triggerSyncRequest = vi.fn().mockResolvedValueOnce(false).mockResolvedValueOnce(true);
    const refreshClusterState = vi.fn(async () => {});

    await runClusterAutoSyncTick(triggerSyncRequest, refreshClusterState, pushRuntimeEvent);
    expect(refreshClusterState).not.toHaveBeenCalled();

    await runClusterAutoSyncTick(triggerSyncRequest, refreshClusterState, pushRuntimeEvent);
    expect(refreshClusterState).toHaveBeenCalledTimes(1);
  });
});
