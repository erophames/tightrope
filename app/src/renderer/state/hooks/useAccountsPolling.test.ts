import { describe, expect, test, vi } from 'vitest';
import {
  ACCOUNT_FAST_REFRESH_FAILED_MESSAGE,
  ACCOUNT_POLLING_FAILED_MESSAGE,
  reportAccountPollingFailureOnce,
  reportFastAccountRefreshFailureOnce,
  resetAccountRefreshErrorFlags,
  tryStartFastAccountRefresh,
} from './useAccountsPolling';

describe('useAccountsPolling', () => {
  test('reportAccountPollingFailureOnce reports only once until reset', () => {
    const ref = { current: false };
    const pushRuntimeEvent = vi.fn();

    reportAccountPollingFailureOnce(ref, pushRuntimeEvent);
    reportAccountPollingFailureOnce(ref, pushRuntimeEvent);

    expect(pushRuntimeEvent).toHaveBeenCalledTimes(1);
    expect(pushRuntimeEvent).toHaveBeenCalledWith(ACCOUNT_POLLING_FAILED_MESSAGE, 'warn');

    resetAccountRefreshErrorFlags(ref, { current: true });
    reportAccountPollingFailureOnce(ref, pushRuntimeEvent);
    expect(pushRuntimeEvent).toHaveBeenCalledTimes(2);
  });

  test('reportFastAccountRefreshFailureOnce reports only once until reset', () => {
    const ref = { current: false };
    const pushRuntimeEvent = vi.fn();

    reportFastAccountRefreshFailureOnce(ref, pushRuntimeEvent);
    reportFastAccountRefreshFailureOnce(ref, pushRuntimeEvent);

    expect(pushRuntimeEvent).toHaveBeenCalledTimes(1);
    expect(pushRuntimeEvent).toHaveBeenCalledWith(ACCOUNT_FAST_REFRESH_FAILED_MESSAGE, 'warn');
  });

  test('tryStartFastAccountRefresh enforces cooldown', () => {
    const lastRef = { current: 1000 };

    expect(tryStartFastAccountRefresh(lastRef, 1100, 250)).toBe(false);
    expect(lastRef.current).toBe(1000);

    expect(tryStartFastAccountRefresh(lastRef, 1300, 250)).toBe(true);
    expect(lastRef.current).toBe(1300);
  });
});
