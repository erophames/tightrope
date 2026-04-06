import type {
  AddAccountStep,
  AppRuntimeState,
  OauthStartResponse,
  OauthStatusResponse,
} from '../../shared/types';
import { callbackParts, isValidOAuthAuthorizationUrl } from './useOAuthFlowHelpers';

export function applyOauthStatusState(
  previous: AppRuntimeState,
  status: OauthStatusResponse,
): AppRuntimeState {
  const callbackUrl = status.callbackUrl ?? previous.authState.listenerUrl;
  const parts = callbackParts(callbackUrl);
  const listenerRunning = status.listenerRunning ?? status.status === 'pending';
  const nextAuthorizationUrl = isValidOAuthAuthorizationUrl(status.authorizationUrl)
    ? status.authorizationUrl
    : previous.browserAuthUrl;
  return {
    ...previous,
    authState: {
      ...previous.authState,
      callbackPath: parts.callbackPath,
      listenerPort: parts.listenerPort,
      listenerUrl: parts.listenerUrl,
      listenerRunning,
      initStatus: status.status,
      lastResponse: status.errorMessage ?? previous.authState.lastResponse,
    },
    browserAuthUrl: nextAuthorizationUrl,
  };
}

export function applyOauthListenerStartedState(
  previous: AppRuntimeState,
  callbackUrl: string,
  authorizationUrl: string | null | undefined,
): AppRuntimeState {
  const parts = callbackParts(callbackUrl);
  return {
    ...previous,
    browserAuthUrl: isValidOAuthAuthorizationUrl(authorizationUrl) ? authorizationUrl : previous.browserAuthUrl,
    authState: {
      ...previous.authState,
      callbackPath: parts.callbackPath,
      listenerPort: parts.listenerPort,
      listenerUrl: parts.listenerUrl,
      listenerRunning: true,
      initStatus: 'pending',
      lastResponse: previous.authState.lastResponse,
    },
  };
}

export function applyOauthListenerStoppedState(previous: AppRuntimeState): AppRuntimeState {
  return {
    ...previous,
    authState: {
      ...previous.authState,
      listenerRunning: false,
      initStatus: 'listener stopped',
    },
  };
}

export function applyOauthListenerErrorState(previous: AppRuntimeState, message: string): AppRuntimeState {
  return {
    ...previous,
    authState: {
      ...previous.authState,
      initStatus: 'error',
      lastResponse: message,
    },
  };
}

export function applyCapturedCallbackState(previous: AppRuntimeState, stampedResponse: string): AppRuntimeState {
  return {
    ...previous,
    authState: {
      ...previous.authState,
      lastResponse: stampedResponse,
    },
  };
}

export function applyAddAccountStepState(previous: AppRuntimeState, step: AddAccountStep): AppRuntimeState {
  return {
    ...previous,
    addAccountStep: step,
  };
}

export function applySelectedFileNameState(previous: AppRuntimeState, fileName: string): AppRuntimeState {
  return {
    ...previous,
    selectedFileName: fileName,
  };
}

export function applyManualCallbackState(previous: AppRuntimeState, manualCallback: string): AppRuntimeState {
  return {
    ...previous,
    manualCallback,
  };
}

export function applyAddAccountSuccessState(
  previous: AppRuntimeState,
  payload: { email: string; plan: string; selectedAccountDetailId?: string | null },
): AppRuntimeState {
  return {
    ...previous,
    addAccountStep: 'stepSuccess',
    successEmail: payload.email,
    successPlan: payload.plan,
    selectedAccountDetailId: payload.selectedAccountDetailId ?? previous.selectedAccountDetailId,
    addAccountError: 'Something went wrong.',
  };
}

export function applyAddAccountErrorState(previous: AppRuntimeState, message: string): AppRuntimeState {
  return {
    ...previous,
    addAccountStep: 'stepError',
    addAccountError: message,
  };
}

export function applyDeviceFlowStartedState(
  previous: AppRuntimeState,
  start: OauthStartResponse,
  expiresInSeconds: number,
): AppRuntimeState {
  return {
    ...previous,
    addAccountStep: 'stepDevice',
    deviceVerifyUrl: start.verificationUrl ?? previous.deviceVerifyUrl,
    deviceUserCode: start.userCode ?? previous.deviceUserCode,
    deviceCountdownSeconds: expiresInSeconds,
  };
}

export function applyDeviceFlowCancelledState(
  previous: AppRuntimeState,
  deviceCountdownSeconds: number,
): AppRuntimeState {
  return {
    ...previous,
    addAccountStep: 'stepMethod',
    deviceCountdownSeconds,
  };
}
