import type { SetStateAction } from 'react';
import { afterEach, describe, expect, test, vi } from 'vitest';
import { createInitialRuntimeState } from '../../test/fixtures/seed';
import type { AddAccountStep, AppRuntimeState, OauthStatusResponse } from '../../shared/types';
import { createOAuthFlowDeviceActions } from './useOAuthFlowDeviceActions';
import { DEFAULT_DEVICE_EXPIRES_SECONDS } from './useOAuthFlowHelpers';

interface DeviceActionDepsOverride {
  oauthStartRequest?: () => Promise<{
    method: string;
    authorizationUrl: string | null;
    callbackUrl: string | null;
    verificationUrl: string | null;
    userCode: string | null;
    deviceAuthId: string | null;
    intervalSeconds: number | null;
    expiresInSeconds: number | null;
  }>;
  oauthStatusRequest?: () => Promise<OauthStatusResponse>;
  oauthCompleteRequest?: (body: { deviceAuthId?: string; userCode?: string }) => Promise<{ status: string }>;
}

function createDeviceActionHarness(overrides: DeviceActionDepsOverride = {}) {
  const stateRef = { current: createInitialRuntimeState() };
  const setState = vi.fn((update: SetStateAction<AppRuntimeState>) => {
    stateRef.current = typeof update === 'function' ? update(stateRef.current) : update;
  });
  const deviceTimerRef = { current: null as ReturnType<typeof setInterval> | null };
  const oauthPollRef = { current: null as ReturnType<typeof setInterval> | null };
  const clearDeviceFlowTimers = vi.fn(() => {
    if (deviceTimerRef.current) {
      clearInterval(deviceTimerRef.current);
      deviceTimerRef.current = null;
    }
    if (oauthPollRef.current) {
      clearInterval(oauthPollRef.current);
      oauthPollRef.current = null;
    }
  });
  const captureOauthAccountBaseline = vi.fn();
  const setFlowPhase = vi.fn((_phase: AddAccountStep) => {});
  const setFlowError = vi.fn();
  const setAddAccountErrorState = vi.fn();
  const finalizeOauthAccountSuccess = vi.fn(async () => {});
  const oauthStartRequest =
    overrides.oauthStartRequest ??
    vi.fn(async () => ({
      method: 'device',
      authorizationUrl: null,
      callbackUrl: null,
      verificationUrl: 'https://example.com/verify',
      userCode: 'ABCD-1234',
      deviceAuthId: 'dev-1',
      intervalSeconds: 1,
      expiresInSeconds: 3,
    }));
  const oauthStatusRequest =
    overrides.oauthStatusRequest ??
    vi.fn(async () => ({
      status: 'pending',
      errorMessage: null,
      callbackUrl: null,
      authorizationUrl: null,
    }));
  const oauthCompleteRequest =
    overrides.oauthCompleteRequest ??
    vi.fn(async () => ({
      status: 'pending',
    }));

  const actions = createOAuthFlowDeviceActions({
    state: stateRef.current,
    setState,
    oauthStartRequest,
    oauthStatusRequest,
    oauthCompleteRequest,
    clearDeviceFlowTimers,
    captureOauthAccountBaseline,
    setFlowPhase,
    setFlowError,
    setAddAccountErrorState,
    finalizeOauthAccountSuccess,
    deviceTimerRef,
    oauthPollRef,
  });

  return {
    stateRef,
    clearDeviceFlowTimers,
    captureOauthAccountBaseline,
    setFlowPhase,
    setFlowError,
    setAddAccountErrorState,
    finalizeOauthAccountSuccess,
    oauthStartRequest,
    oauthStatusRequest,
    oauthCompleteRequest,
    actions,
  };
}

describe('useOAuthFlowDeviceActions', () => {
  afterEach(() => {
    vi.useRealTimers();
  });

  test('startDeviceFlow initializes state and decrements countdown on timer ticks', async () => {
    vi.useFakeTimers();
    const harness = createDeviceActionHarness();

    await harness.actions.startDeviceFlow();
    expect(harness.captureOauthAccountBaseline).toHaveBeenCalled();
    expect(harness.setFlowPhase).toHaveBeenCalledWith('stepDevice');
    expect(harness.stateRef.current.deviceCountdownSeconds).toBe(3);

    await vi.advanceTimersByTimeAsync(1000);
    expect(harness.stateRef.current.deviceCountdownSeconds).toBe(2);

    harness.clearDeviceFlowTimers();
  });

  test('startDeviceFlow finalizes when status poll returns success', async () => {
    vi.useFakeTimers();
    const harness = createDeviceActionHarness({
      oauthStatusRequest: vi.fn(async () => ({
        status: 'success',
        errorMessage: null,
        callbackUrl: 'http://localhost:1455/auth/callback?code=device',
        authorizationUrl: null,
      })),
    });

    await harness.actions.startDeviceFlow();
    await vi.advanceTimersByTimeAsync(1000);

    expect(harness.finalizeOauthAccountSuccess).toHaveBeenCalledWith(
      'http://localhost:1455/auth/callback?code=device',
      'AAAA-BBBB',
      'device oauth completed and account imported',
    );

    harness.clearDeviceFlowTimers();
  });

  test('startDeviceFlow reports startup failures', async () => {
    const harness = createDeviceActionHarness({
      oauthStartRequest: vi.fn(async () => {
        throw new Error('device start failed');
      }),
    });

    await harness.actions.startDeviceFlow();

    expect(harness.setAddAccountErrorState).toHaveBeenCalledWith('device start failed');
  });

  test('cancelDeviceFlow clears timers and restores method step defaults', () => {
    const harness = createDeviceActionHarness();
    harness.stateRef.current = {
      ...harness.stateRef.current,
      addAccountStep: 'stepDevice',
      deviceCountdownSeconds: 42,
    };

    harness.actions.cancelDeviceFlow();

    expect(harness.clearDeviceFlowTimers).toHaveBeenCalled();
    expect(harness.setFlowPhase).toHaveBeenCalledWith('stepMethod');
    expect(harness.stateRef.current.addAccountStep).toBe('stepMethod');
    expect(harness.stateRef.current.deviceCountdownSeconds).toBe(DEFAULT_DEVICE_EXPIRES_SECONDS);
  });
});
