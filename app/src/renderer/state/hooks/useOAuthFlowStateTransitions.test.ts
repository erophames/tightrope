import { describe, expect, test } from 'vitest';
import { createInitialRuntimeState } from '../../test/fixtures/seed';
import type { OauthStartResponse, OauthStatusResponse } from '../../shared/types';
import {
  applyAddAccountErrorState,
  applyAddAccountSuccessState,
  applyCapturedCallbackState,
  applyDeviceFlowCancelledState,
  applyDeviceFlowStartedState,
  applyOauthListenerErrorState,
  applyOauthListenerStartedState,
  applyOauthStatusState,
} from './useOAuthFlowStateTransitions';

describe('useOAuthFlowStateTransitions', () => {
  test('applyOauthStatusState maps callback and listener status fields', () => {
    const initial = createInitialRuntimeState();
    const status: OauthStatusResponse = {
      status: 'pending',
      errorMessage: null,
      callbackUrl: 'http://localhost:1455/auth/callback',
      authorizationUrl: 'https://auth.openai.com/oauth/authorize?response_type=code&state=state-1',
    };

    const next = applyOauthStatusState(initial, status);

    expect(next.authState.callbackPath).toBe('/auth/callback');
    expect(next.authState.listenerPort).toBe(1455);
    expect(next.authState.listenerRunning).toBe(true);
    expect(next.authState.initStatus).toBe('pending');
    expect(next.browserAuthUrl).toContain('response_type=code');
  });

  test('applyOauthListenerStartedState preserves previous auth URL when incoming URL is invalid', () => {
    const initial = {
      ...createInitialRuntimeState(),
      browserAuthUrl: 'https://auth.openai.com/oauth/authorize?response_type=code&state=kept',
    };

    const next = applyOauthListenerStartedState(initial, 'http://localhost:1455/auth/callback', '...');

    expect(next.authState.listenerRunning).toBe(true);
    expect(next.authState.initStatus).toBe('pending');
    expect(next.browserAuthUrl).toContain('state=kept');
  });

  test('applyOauthListenerErrorState sets auth error payload', () => {
    const initial = createInitialRuntimeState();

    const next = applyOauthListenerErrorState(initial, 'listener failed');

    expect(next.authState.initStatus).toBe('error');
    expect(next.authState.lastResponse).toBe('listener failed');
  });

  test('applyCapturedCallbackState updates auth lastResponse only', () => {
    const initial = createInitialRuntimeState();

    const next = applyCapturedCallbackState(initial, '12:00:00 code_abcd');

    expect(next.authState.lastResponse).toBe('12:00:00 code_abcd');
    expect(next.addAccountStep).toBe(initial.addAccountStep);
  });

  test('applyAddAccountSuccessState sets success fields and selected account', () => {
    const initial = createInitialRuntimeState();

    const next = applyAddAccountSuccessState(initial, {
      email: 'alice@test.local',
      plan: 'openai',
      selectedAccountDetailId: 'acc-1',
    });

    expect(next.addAccountStep).toBe('stepSuccess');
    expect(next.successEmail).toBe('alice@test.local');
    expect(next.successPlan).toBe('openai');
    expect(next.selectedAccountDetailId).toBe('acc-1');
    expect(next.addAccountError).toBe('Something went wrong.');
  });

  test('applyAddAccountErrorState and applyDeviceFlowStartedState set expected fields', () => {
    const initial = createInitialRuntimeState();
    const start: OauthStartResponse = {
      method: 'device',
      authorizationUrl: null,
      callbackUrl: null,
      verificationUrl: 'https://example.com/verify',
      userCode: 'ABCD-1234',
      deviceAuthId: 'dev-1',
      intervalSeconds: 5,
      expiresInSeconds: 900,
    };

    const device = applyDeviceFlowStartedState(initial, start, 777);
    const errored = applyAddAccountErrorState(device, 'device flow failed');

    expect(device.addAccountStep).toBe('stepDevice');
    expect(device.deviceVerifyUrl).toBe('https://example.com/verify');
    expect(device.deviceUserCode).toBe('ABCD-1234');
    expect(device.deviceCountdownSeconds).toBe(777);
    expect(errored.addAccountStep).toBe('stepError');
    expect(errored.addAccountError).toBe('device flow failed');
  });

  test('applyDeviceFlowCancelledState resets step and countdown', () => {
    const initial = {
      ...createInitialRuntimeState(),
      addAccountStep: 'stepDevice' as const,
      deviceCountdownSeconds: 42,
    };

    const next = applyDeviceFlowCancelledState(initial, 900);

    expect(next.addAccountStep).toBe('stepMethod');
    expect(next.deviceCountdownSeconds).toBe(900);
  });
});
