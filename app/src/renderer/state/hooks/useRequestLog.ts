import { useCallback, useEffect, useRef, type Dispatch, type SetStateAction } from 'react';
import type { TightropeService } from '../../services/tightrope';
import type { AppRuntimeState, RouteRow, RuntimeRequestLog } from '../../shared/types';

export interface UseRequestLogOptions {
  refreshMs: number;
  requestLogsLimit: number;
  setState: Dispatch<SetStateAction<AppRuntimeState>>;
  mapRuntimeRequestLog: (log: RuntimeRequestLog) => RouteRow;
  shouldTriggerImmediateAccountsRefresh: (rows: RouteRow[]) => boolean;
  triggerFastAccountRefresh: () => void;
  listRequestLogsRequest: TightropeService['listRequestLogsRequest'];
  reportPollingError?: (message: string) => void;
}

interface UseRequestLogResult {
  refreshRequestLogs: () => Promise<void>;
}

export function useRequestLog(options: UseRequestLogOptions): UseRequestLogResult {
  const { listRequestLogsRequest } = options;
  const refreshInFlightRef = useRef(false);
  const pollErrorReportedRef = useRef(false);

  const refreshRequestLogs = useCallback(async (): Promise<void> => {
    if (refreshInFlightRef.current) {
      return;
    }

    refreshInFlightRef.current = true;
    try {
      const logs = await listRequestLogsRequest(options.requestLogsLimit, 0);
      const rows = logs.map(options.mapRuntimeRequestLog);
      options.setState((previous) => ({
        ...previous,
        rows,
        drawerRowId: previous.drawerRowId && rows.some((row) => row.id === previous.drawerRowId) ? previous.drawerRowId : null,
      }));

      if (options.shouldTriggerImmediateAccountsRefresh(rows)) {
        options.triggerFastAccountRefresh();
      }
      pollErrorReportedRef.current = false;
    } finally {
      refreshInFlightRef.current = false;
    }
  }, [listRequestLogsRequest, options]);

  useEffect(() => {
    const handle = setInterval(() => {
      void refreshRequestLogs().catch(() => {
        if (pollErrorReportedRef.current) {
          return;
        }
        pollErrorReportedRef.current = true;
        options.reportPollingError?.('request log polling failed; retrying');
      });
    }, options.refreshMs);

    return () => {
      clearInterval(handle);
    };
  }, [options.refreshMs, refreshRequestLogs]);

  return { refreshRequestLogs };
}
