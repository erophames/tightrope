import type { MutableRefObject } from 'react';
import type { StatusNoticeLevel } from '../statusNotices';

type PushRuntimeEvent = (text: string, level?: StatusNoticeLevel) => void;

export const ACCOUNT_POLLING_FAILED_MESSAGE = 'account polling failed; retrying';
export const ACCOUNT_FAST_REFRESH_FAILED_MESSAGE = 'account refresh failed; retrying';

export function resetAccountRefreshErrorFlags(
  accountsPollErrorReportedRef: MutableRefObject<boolean>,
  fastRefreshErrorReportedRef: MutableRefObject<boolean>,
): void {
  accountsPollErrorReportedRef.current = false;
  fastRefreshErrorReportedRef.current = false;
}

export function reportAccountPollingFailureOnce(
  accountsPollErrorReportedRef: MutableRefObject<boolean>,
  pushRuntimeEvent: PushRuntimeEvent,
): void {
  if (accountsPollErrorReportedRef.current) {
    return;
  }
  accountsPollErrorReportedRef.current = true;
  pushRuntimeEvent(ACCOUNT_POLLING_FAILED_MESSAGE, 'warn');
}

export function reportFastAccountRefreshFailureOnce(
  fastRefreshErrorReportedRef: MutableRefObject<boolean>,
  pushRuntimeEvent: PushRuntimeEvent,
): void {
  if (fastRefreshErrorReportedRef.current) {
    return;
  }
  fastRefreshErrorReportedRef.current = true;
  pushRuntimeEvent(ACCOUNT_FAST_REFRESH_FAILED_MESSAGE, 'warn');
}

export function tryStartFastAccountRefresh(
  lastFastAccountRefreshMsRef: MutableRefObject<number>,
  nowMs: number,
  cooldownMs: number,
): boolean {
  if (nowMs - lastFastAccountRefreshMsRef.current < cooldownMs) {
    return false;
  }

  lastFastAccountRefreshMsRef.current = nowMs;
  return true;
}
