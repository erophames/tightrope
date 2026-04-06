import { act, renderHook, waitFor } from '@testing-library/react';
import { useState } from 'react';
import { describe, expect, it, vi } from 'vitest';
import { accountsSeed, createInitialRuntimeState } from '../../test/fixtures/seed';
import type { Account, AppRuntimeState, RuntimeAccount } from '../../shared/types';
import { useOAuthFlow, type UseOAuthFlowOptions } from './useOAuthFlow';

type OAuthServiceMocks = Pick<
  UseOAuthFlowOptions,
  | 'oauthStartRequest'
  | 'oauthStatusRequest'
  | 'oauthStopRequest'
  | 'oauthRestartRequest'
  | 'oauthCompleteRequest'
  | 'oauthManualCallbackRequest'
  | 'importAccountRequest'
  | 'onOauthDeepLinkRequest'
>;

function createOAuthServiceMocks(overrides: Partial<OAuthServiceMocks> = {}): OAuthServiceMocks {
  return {
    oauthStartRequest: vi.fn(async () => ({
      method: 'browser',
      authorizationUrl: null,
      callbackUrl: null,
      verificationUrl: null,
      userCode: null,
      deviceAuthId: null,
      intervalSeconds: null,
      expiresInSeconds: null,
    })),
    oauthStatusRequest: vi.fn(async () => ({
      status: 'idle',
      errorMessage: null,
      callbackUrl: null,
      authorizationUrl: null,
      listenerRunning: false,
    })),
    oauthStopRequest: vi.fn(async () => ({ status: 'stopped', errorMessage: null })),
    oauthRestartRequest: vi.fn(async () => ({
      method: 'browser',
      authorizationUrl: null,
      callbackUrl: null,
      verificationUrl: null,
      userCode: null,
      deviceAuthId: null,
      intervalSeconds: null,
      expiresInSeconds: null,
    })),
    oauthCompleteRequest: vi.fn(async () => ({ status: 'pending' })),
    oauthManualCallbackRequest: vi.fn(async () => ({ status: 'error', errorMessage: 'not configured' })),
    importAccountRequest: vi.fn(async (email: string, provider: string) => ({
      accountId: 'imported',
      email,
      provider,
      status: 'active',
    })),
    onOauthDeepLinkRequest: vi.fn(() => null),
    ...overrides,
  };
}

describe('useOAuthFlow', () => {
  it('starts browser listener and updates auth state from callback URL', async () => {
    const pushRuntimeEvent = vi.fn();
    const refreshAccountsFromNative = vi.fn(async (): Promise<RuntimeAccount[]> => []);
    const refreshUsageTelemetryAfterAccountAdd = vi.fn(async () => {});
    const service = createOAuthServiceMocks({
      oauthStartRequest: vi.fn(async () => ({
        method: 'browser',
        authorizationUrl: 'https://auth.openai.com/oauth/authorize?response_type=code&state=state-hook',
        callbackUrl: 'http://localhost:1455/auth/callback',
        verificationUrl: null,
        userCode: null,
        deviceAuthId: null,
        intervalSeconds: null,
        expiresInSeconds: null,
      })),
    });

    const { result } = renderHook(() => {
      const [state, setState] = useState<AppRuntimeState>(createInitialRuntimeState());
      const oauth = useOAuthFlow({
        state,
        accounts: [],
        setState,
        refreshAccountsFromNative,
        refreshUsageTelemetryAfterAccountAdd,
        pushRuntimeEvent,
        ...service,
      });
      return { state, ...oauth };
    });

    await act(async () => {
      await result.current.createListenerUrl();
    });

    expect(service.oauthStartRequest).toHaveBeenCalledWith('browser');
    expect(result.current.state.authState.listenerRunning).toBe(true);
    expect(result.current.state.authState.callbackPath).toBe('/auth/callback');
    expect(result.current.state.authState.listenerPort).toBe(1455);
    expect(result.current.state.browserAuthUrl).toContain('response_type=code');
  });

  it('submits manual callback and transitions to success after account refresh', async () => {
    const pushRuntimeEvent = vi.fn();
    const refreshAccountsFromNative = vi.fn(async (): Promise<RuntimeAccount[]> => [
      {
        accountId: 'acc-1',
        email: 'alice@test.local',
        provider: 'openai',
        status: 'active',
      },
    ]);
    const refreshUsageTelemetryAfterAccountAdd = vi.fn(async () => {});
    const service = createOAuthServiceMocks({
      oauthManualCallbackRequest: vi.fn(async () => ({
        status: 'success',
        errorMessage: null,
      })),
    });

    const { result } = renderHook(() => {
      const [state, setState] = useState<AppRuntimeState>(createInitialRuntimeState());
      const oauth = useOAuthFlow({
        state,
        accounts: [],
        setState,
        refreshAccountsFromNative,
        refreshUsageTelemetryAfterAccountAdd,
        pushRuntimeEvent,
        ...service,
      });
      return { state, ...oauth };
    });

    const callbackUrl = 'http://localhost:1455/auth/callback?code=abc123&state=hook';

    act(() => {
      result.current.setManualCallback(callbackUrl);
    });

    await act(async () => {
      await result.current.submitManualCallback();
    });

    expect(service.oauthManualCallbackRequest).toHaveBeenCalledWith(callbackUrl);
    expect(result.current.state.addAccountStep).toBe('stepSuccess');
    expect(result.current.state.successEmail).toBe('alice@test.local');
    expect(result.current.state.selectedAccountDetailId).toBe('acc-1');
    expect(refreshUsageTelemetryAfterAccountAdd).toHaveBeenCalledWith('acc-1', 'alice@test.local');
  });

  it('guards copy when browser auth URL is missing and copies after listener start', async () => {
    const pushRuntimeEvent = vi.fn();
    const refreshAccountsFromNative = vi.fn(async (): Promise<RuntimeAccount[]> => []);
    const refreshUsageTelemetryAfterAccountAdd = vi.fn(async () => {});
    const writeText = vi.fn(async () => {});
    const service = createOAuthServiceMocks({
      oauthStartRequest: vi.fn(async () => ({
        method: 'browser',
        authorizationUrl: 'https://auth.openai.com/oauth/authorize?response_type=code&state=copy-test',
        callbackUrl: 'http://localhost:1455/auth/callback',
        verificationUrl: null,
        userCode: null,
        deviceAuthId: null,
        intervalSeconds: null,
        expiresInSeconds: null,
      })),
    });

    Object.defineProperty(navigator, 'clipboard', {
      configurable: true,
      value: { writeText },
    });

    const { result } = renderHook(() => {
      const [state, setState] = useState<AppRuntimeState>(createInitialRuntimeState());
      const oauth = useOAuthFlow({
        state,
        accounts: [],
        setState,
        refreshAccountsFromNative,
        refreshUsageTelemetryAfterAccountAdd,
        pushRuntimeEvent,
        ...service,
      });
      return { state, ...oauth };
    });

    await act(async () => {
      await result.current.copyBrowserAuthUrl();
    });

    expect(pushRuntimeEvent).toHaveBeenCalledWith('No browser authorization URL to copy', 'warn');

    await act(async () => {
      await result.current.createListenerUrl();
    });

    await act(async () => {
      await result.current.copyBrowserAuthUrl();
    });

    await waitFor(() => {
      expect(writeText).toHaveBeenCalledWith(
        'https://auth.openai.com/oauth/authorize?response_type=code&state=copy-test',
      );
    });
    expect(result.current.state.copyAuthLabel).toBe('Copied');
  });

  it('auto-closes waiting oauth dialog when a new account is added', async () => {
    const pushRuntimeEvent = vi.fn();
    const refreshAccountsFromNative = vi.fn(async (): Promise<RuntimeAccount[]> => []);
    const refreshUsageTelemetryAfterAccountAdd = vi.fn(async () => {});
    const service = createOAuthServiceMocks();

    const baselineAccount: Account = { ...accountsSeed[0], id: 'acc-baseline' };
    const addedAccount: Account = { ...accountsSeed[1], id: 'acc-added' };

    const { result } = renderHook(() => {
      const [state, setState] = useState<AppRuntimeState>(createInitialRuntimeState());
      const [accounts, setAccounts] = useState<Account[]>([baselineAccount]);
      const oauth = useOAuthFlow({
        state,
        accounts,
        setState,
        refreshAccountsFromNative,
        refreshUsageTelemetryAfterAccountAdd,
        pushRuntimeEvent,
        ...service,
      });
      return { state, setAccounts, ...oauth };
    });

    act(() => {
      result.current.openAddAccountDialog();
      result.current.setAddAccountStep('stepBrowser');
    });

    expect(result.current.state.addAccountOpen).toBe(true);
    expect(result.current.state.addAccountStep).toBe('stepBrowser');

    act(() => {
      result.current.setAccounts([baselineAccount, addedAccount]);
    });

    await waitFor(() => {
      expect(result.current.state.addAccountOpen).toBe(false);
    });
    expect(result.current.state.addAccountStep).toBe('stepMethod');
    expect(pushRuntimeEvent).toHaveBeenCalledWith('oauth dialog closed after account import', 'success');
  });
});
