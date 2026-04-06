import type { SetStateAction } from 'react';
import { afterEach, describe, expect, test, vi } from 'vitest';
import { createInitialRuntimeState } from '../../test/fixtures/seed';
import type { AppRuntimeState, OauthStatusResponse } from '../../shared/types';
import { createOAuthFlowBrowserActions } from './useOAuthFlowBrowserActions';
import { BROWSER_OAUTH_POLL_MS } from './useOAuthFlowHelpers';

interface BrowserActionDepsOverride {
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
  oauthStopRequest?: () => Promise<OauthStatusResponse>;
  oauthRestartRequest?: () => Promise<{
    method: string;
    authorizationUrl: string | null;
    callbackUrl: string | null;
    verificationUrl: string | null;
    userCode: string | null;
    deviceAuthId: string | null;
    intervalSeconds: number | null;
    expiresInSeconds: number | null;
  }>;
  stopBrowserOauthPolling?: () => void;
  setAddAccountErrorState?: (message: string, optionsOverride?: { syncFlowError?: boolean; reportRuntimeEvent?: boolean }) => void;
  applyOauthStatus?: (status: OauthStatusResponse) => void;
  finalizeOauthAccountSuccess?: (
    callbackUrl: string | null,
    fallbackHint: string,
    successEvent: string,
    successOptions?: { autoClose?: boolean; requireAccountVisible?: boolean },
  ) => Promise<void>;
}

function createBrowserActionHarness(overrides: BrowserActionDepsOverride = {}) {
  const stateRef = { current: createInitialRuntimeState() };
  const setState = vi.fn((update: SetStateAction<AppRuntimeState>) => {
    stateRef.current = typeof update === 'function' ? update(stateRef.current) : update;
  });
  const pushRuntimeEvent = vi.fn();
  const browserOauthPollRef = { current: null as ReturnType<typeof setInterval> | null };
  const oauthStartInFlightRef = { current: null as Promise<string | null> | null };
  const oauthDeepLinkFinalizeInFlightRef = { current: false };
  const stopBrowserOauthPolling =
    overrides.stopBrowserOauthPolling ??
    vi.fn(() => {
      if (browserOauthPollRef.current) {
        clearInterval(browserOauthPollRef.current);
        browserOauthPollRef.current = null;
      }
    });
  const setAddAccountErrorState = overrides.setAddAccountErrorState ?? vi.fn();
  const applyOauthStatus = overrides.applyOauthStatus ?? vi.fn();
  const finalizeOauthAccountSuccess = overrides.finalizeOauthAccountSuccess ?? vi.fn(async () => {});
  const oauthStartRequest =
    overrides.oauthStartRequest ??
    vi.fn(async () => ({
      method: 'browser',
      authorizationUrl: 'https://auth.openai.com/oauth/authorize?response_type=code&state=test-state',
      callbackUrl: 'http://localhost:1455/auth/callback',
      verificationUrl: null,
      userCode: null,
      deviceAuthId: null,
      intervalSeconds: null,
      expiresInSeconds: null,
    }));
  const oauthStatusRequest =
    overrides.oauthStatusRequest ??
    vi.fn(async () => ({
      status: 'pending',
      errorMessage: null,
      callbackUrl: null,
      authorizationUrl: null,
    }));
  const oauthStopRequest =
    overrides.oauthStopRequest ??
    vi.fn(async () => ({
      status: 'stopped',
      errorMessage: null,
      callbackUrl: null,
      authorizationUrl: null,
    }));
  const oauthRestartRequest =
    overrides.oauthRestartRequest ??
    vi.fn(async () => ({
      method: 'browser',
      authorizationUrl: 'https://auth.openai.com/oauth/authorize?response_type=code&state=restart',
      callbackUrl: 'http://localhost:1455/auth/callback',
      verificationUrl: null,
      userCode: null,
      deviceAuthId: null,
      intervalSeconds: null,
      expiresInSeconds: null,
    }));

  const actions = createOAuthFlowBrowserActions({
    state: stateRef.current,
    setState,
    pushRuntimeEvent,
    oauthStartRequest,
    oauthStatusRequest,
    oauthStopRequest,
    oauthRestartRequest,
    browserOauthPollRef,
    oauthStartInFlightRef,
    oauthDeepLinkFinalizeInFlightRef,
    stopBrowserOauthPolling,
    setAddAccountErrorState,
    applyOauthStatus,
    finalizeOauthAccountSuccess,
  });

  return {
    stateRef,
    setState,
    pushRuntimeEvent,
    browserOauthPollRef,
    stopBrowserOauthPolling,
    setAddAccountErrorState,
    applyOauthStatus,
    finalizeOauthAccountSuccess,
    oauthStartRequest,
    oauthStatusRequest,
    actions,
  };
}

describe('useOAuthFlowBrowserActions', () => {
  afterEach(() => {
    vi.useRealTimers();
  });

  test('createListenerUrl stores listener metadata and returns authorization URL', async () => {
    const { actions, stateRef, pushRuntimeEvent } = createBrowserActionHarness();

    const authorizationUrl = await actions.createListenerUrl();

    expect(authorizationUrl).toContain('response_type=code');
    expect(stateRef.current.authState.listenerRunning).toBe(true);
    expect(stateRef.current.authState.listenerUrl).toContain('/auth/callback');
    expect(pushRuntimeEvent).toHaveBeenCalledWith(expect.stringContaining('callback URL generated'));
  });

  test('createListenerUrl surfaces listener bootstrap failures', async () => {
    const { actions, stateRef, pushRuntimeEvent } = createBrowserActionHarness({
      oauthStartRequest: vi.fn(async () => {
        throw new Error('listener start failed');
      }),
    });

    const authorizationUrl = await actions.createListenerUrl();

    expect(authorizationUrl).toBeNull();
    expect(stateRef.current.authState.initStatus).toBe('error');
    expect(stateRef.current.authState.lastResponse).toBe('listener start failed');
    expect(pushRuntimeEvent).toHaveBeenCalledWith('listener start failed', 'warn');
  });

  test('startBrowserOauthPolling processes interval tick and reports error status', async () => {
    vi.useFakeTimers();
    const oauthStatusRequest = vi
      .fn()
      .mockResolvedValueOnce({
        status: 'pending',
        errorMessage: null,
        callbackUrl: null,
        authorizationUrl: null,
      })
      .mockResolvedValueOnce({
        status: 'error',
        errorMessage: 'oauth failed',
        callbackUrl: null,
        authorizationUrl: null,
      });
    const setAddAccountErrorState = vi.fn();
    const harness = createBrowserActionHarness({
      oauthStatusRequest,
      setAddAccountErrorState,
    });

    harness.actions.startBrowserOauthPolling();
    await Promise.resolve();
    await vi.advanceTimersByTimeAsync(BROWSER_OAUTH_POLL_MS);

    expect(oauthStatusRequest).toHaveBeenCalledTimes(2);
    expect(setAddAccountErrorState).toHaveBeenCalledWith('oauth failed');
    expect(harness.stopBrowserOauthPolling).toHaveBeenCalled();
  });

  test('completeBrowserOauthFromDeepLink finalizes successful callback', async () => {
    const oauthStatusRequest = vi.fn(async () => ({
      status: 'success',
      errorMessage: null,
      callbackUrl: 'http://localhost:1455/auth/callback?code=abc',
      authorizationUrl: null,
    }));
    const finalizeOauthAccountSuccess = vi.fn(async () => {});
    const { actions, stopBrowserOauthPolling } = createBrowserActionHarness({
      oauthStatusRequest,
      finalizeOauthAccountSuccess,
    });

    await actions.completeBrowserOauthFromDeepLink('tightrope://oauth/callback?code=abc');

    expect(stopBrowserOauthPolling).toHaveBeenCalled();
    expect(finalizeOauthAccountSuccess).toHaveBeenCalledWith(
      'http://localhost:1455/auth/callback?code=abc',
      'tightrope://oauth/callback?code=abc',
      'browser oauth completed and account imported',
      { autoClose: true, requireAccountVisible: true },
    );
  });
});
