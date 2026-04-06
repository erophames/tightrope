import type { MutableRefObject } from 'react';
import type { ClusterStatus, DashboardSettings } from '../../shared/types';
import type { StatusNoticeLevel } from '../statusNotices';
import { shouldScheduleAutoSync } from '../logic';
import { reportWarn } from '../errors';

type PushRuntimeEvent = (text: string, level?: StatusNoticeLevel) => void;

export const CLUSTER_POLLING_FAILED_MESSAGE = 'cluster polling failed; retrying';

export function reportClusterPollingFailureOnce(
  clusterPollErrorReportedRef: MutableRefObject<boolean>,
  pushRuntimeEvent: PushRuntimeEvent,
): void {
  if (clusterPollErrorReportedRef.current) {
    return;
  }
  clusterPollErrorReportedRef.current = true;
  pushRuntimeEvent(CLUSTER_POLLING_FAILED_MESSAGE, 'warn');
}

export function resetClusterPollingError(clusterPollErrorReportedRef: MutableRefObject<boolean>): void {
  clusterPollErrorReportedRef.current = false;
}

export function canScheduleClusterAutoSync(
  clusterStatus: ClusterStatus,
  appliedDashboardSettings: DashboardSettings,
): boolean {
  return shouldScheduleAutoSync(clusterStatus, appliedDashboardSettings.syncIntervalSeconds);
}

export async function runClusterAutoSyncTick(
  triggerSyncRequest: () => Promise<boolean>,
  refreshClusterState: () => Promise<void>,
  pushRuntimeEvent: PushRuntimeEvent,
): Promise<void> {
  try {
    const triggered = await triggerSyncRequest();
    if (!triggered) {
      return;
    }
    await refreshClusterState();
  } catch (error) {
    reportWarn(pushRuntimeEvent, error, 'Scheduled sync trigger failed');
  }
}
