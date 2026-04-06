import { afterEach, beforeEach, describe, expect, test, vi } from 'vitest';
import type { ElectronApi, OauthDeepLinkEvent, SyncEvent } from '../shared/types';
import {
  addFirewallIpRequest,
  addPeerRequest,
  backendStartRequest,
  backendStatusRequest,
  backendStopRequest,
  clusterDisableRequest,
  clusterEnableRequest,
  applySqlImportRequest,
  changeDatabasePassphraseRequest,
  getClusterStatusRequest,
  getSettingsRequest,
  importAccountRequest,
  listAccountTrafficRequest,
  listAccountsRequest,
  listRequestLogsRequest,
  listStickySessionsRequest,
  onAboutOpenRequest,
  onOauthDeepLinkRequest,
  onSyncEventRequest,
  oauthStatusRequest,
  previewSqlImportRequest,
  refreshAccountUsageTelemetryRequest,
  refreshAccountTokenRequest,
  platformRequest,
  removeFirewallIpRequest,
  removePeerRequest,
  triggerSyncRequest,
  updateSettingsRequest,
  windowCloseRequest,
  windowIsMaximizedRequest,
  windowMinimizeRequest,
  windowToggleMaximizeRequest,
} from './tightrope';

function setTightrope(value: unknown): void {
  Object.defineProperty(window, 'tightrope', {
    writable: true,
    value: value as ElectronApi,
  });
}

describe('tightrope service wrappers', () => {
  let baselineApi: ElectronApi;

  beforeEach(() => {
    baselineApi = window.tightrope;
  });

  afterEach(() => {
    setTightrope(baselineApi);
    vi.restoreAllMocks();
  });

  test('settings wrappers delegate to renderer API methods', async () => {
    const baseSettings = await baselineApi.getSettings();
    const getSettings = vi.fn(async () => ({ ...baseSettings, theme: 'dark' as const }));
    const updateSettings = vi.fn(async (update: Record<string, unknown>) => ({ ...baseSettings, ...update }));
    setTightrope({ ...baselineApi, getSettings, updateSettings });

    const settings = await getSettingsRequest();
    const updated = await updateSettingsRequest({ stickyThreadsEnabled: true });

    expect(getSettings).toHaveBeenCalledTimes(1);
    expect(settings?.theme).toBe('dark');
    expect(updateSettings).toHaveBeenCalledWith({ stickyThreadsEnabled: true });
    expect(updated?.stickyThreadsEnabled).toBe(true);
  });

  test('settings wrappers return null when settings API is unavailable', async () => {
    setTightrope({} as ElectronApi);

    await expect(getSettingsRequest()).resolves.toBeNull();
    await expect(updateSettingsRequest({ theme: 'light' })).resolves.toBeNull();
  });

  test('database passphrase wrapper validates and delegates to renderer API method', async () => {
    const changeDatabasePassphrase = vi.fn(async () => ({ status: 'ok' }));
    setTightrope({ ...baselineApi, changeDatabasePassphrase });

    await expect(changeDatabasePassphraseRequest('old-secret-1', 'new-secret-2')).resolves.toBeUndefined();
    expect(changeDatabasePassphrase).toHaveBeenCalledWith({
      currentPassphrase: 'old-secret-1',
      nextPassphrase: 'new-secret-2',
    });

    await expect(changeDatabasePassphraseRequest('', 'new-secret-2')).rejects.toThrow('Current passphrase is required');
    await expect(changeDatabasePassphraseRequest('old-secret-1', 'short')).rejects.toThrow(
      'New passphrase must be at least 8 characters',
    );
  });

  test('firewall wrappers delegate and return boolean availability flags', async () => {
    const addFirewallIp = vi.fn(async () => ({ ipAddress: '127.0.0.1', createdAt: 'now' }));
    const removeFirewallIp = vi.fn(async () => ({ status: 'deleted' }));
    setTightrope({ ...baselineApi, addFirewallIp, removeFirewallIp });

    await expect(addFirewallIpRequest('127.0.0.1')).resolves.toBe(true);
    await expect(removeFirewallIpRequest('127.0.0.1')).resolves.toBe(true);
    expect(addFirewallIp).toHaveBeenCalledWith('127.0.0.1');
    expect(removeFirewallIp).toHaveBeenCalledWith('127.0.0.1');

    setTightrope({} as ElectronApi);
    await expect(addFirewallIpRequest('127.0.0.1')).resolves.toBe(false);
    await expect(removeFirewallIpRequest('127.0.0.1')).resolves.toBe(false);
  });

  test('cluster wrappers delegate and return availability signals', async () => {
    const clusterEnable = vi.fn(async () => {});
    const clusterDisable = vi.fn(async () => {});
    const addPeer = vi.fn(async () => {});
    const removePeer = vi.fn(async () => {});
    const triggerSync = vi.fn(async () => {});
    const getClusterStatus = vi.fn(async () => await baselineApi.getClusterStatus());
    setTightrope({ ...baselineApi, clusterEnable, clusterDisable, addPeer, removePeer, triggerSync, getClusterStatus });

    await expect(clusterEnableRequest({ cluster_name: 'alpha' })).resolves.toBe(true);
    await expect(clusterDisableRequest()).resolves.toBe(true);
    await expect(addPeerRequest('127.0.0.1:9400')).resolves.toBe(true);
    await expect(removePeerRequest('2')).resolves.toBe(true);
    await expect(triggerSyncRequest()).resolves.toBe(true);
    await expect(getClusterStatusRequest()).resolves.not.toBeNull();

    expect(clusterEnable).toHaveBeenCalledWith({ cluster_name: 'alpha' });
    expect(clusterDisable).toHaveBeenCalledTimes(1);
    expect(addPeer).toHaveBeenCalledWith('127.0.0.1:9400');
    expect(removePeer).toHaveBeenCalledWith('2');
    expect(triggerSync).toHaveBeenCalledTimes(1);
    expect(getClusterStatus).toHaveBeenCalledTimes(1);

    setTightrope({} as ElectronApi);
    await expect(clusterEnableRequest({})).resolves.toBe(false);
    await expect(clusterDisableRequest()).resolves.toBe(false);
    await expect(addPeerRequest('127.0.0.1:9400')).resolves.toBe(false);
    await expect(removePeerRequest('2')).resolves.toBe(false);
    await expect(triggerSyncRequest()).resolves.toBe(false);
    await expect(getClusterStatusRequest()).resolves.toBeNull();
  });

  test('data list wrappers sanitize malformed native payloads', async () => {
    const listAccounts = vi.fn(async () => ({
      accounts: [
        { accountId: 'acc_1', email: 'one@test.local', provider: 'openai', status: 'active' },
        { accountId: 'acc_2' },
      ],
    }));
    const listStickySessions = vi.fn(async () => ({ generatedAtMs: 'oops', sessions: 'bad' }));
    const listRequestLogs = vi.fn(async () => ({
      logs: [
        {
          id: 1,
          path: '/v1/chat/completions',
          method: 'POST',
          statusCode: 200,
          requestedAt: '2026-04-03T12:00:00.000Z',
          totalCost: 0.1,
          accountId: 'acc_1',
          model: 'gpt-5.4',
          errorCode: null,
          transport: 'sse',
        },
        { id: 'bad' },
      ],
    }));
    const listAccountTraffic = vi.fn(async () => ({
      accounts: [
        { accountId: 'acc_1', upBytes: 1, downBytes: 2, lastUpAtMs: 3, lastDownAtMs: 4 },
        { accountId: 'acc_2', upBytes: 'bad' },
      ],
    }));
    setTightrope({
      ...baselineApi,
      listAccounts,
      listStickySessions,
      listRequestLogs,
      listAccountTraffic,
    });

    const accounts = await listAccountsRequest();
    const sessions = await listStickySessionsRequest(50, 0);
    const logs = await listRequestLogsRequest(50, 0);
    const traffic = await listAccountTrafficRequest();

    expect(accounts).toEqual([{ accountId: 'acc_1', email: 'one@test.local', provider: 'openai', status: 'active' }]);
    expect(Array.isArray(sessions.sessions)).toBe(true);
    expect(logs).toHaveLength(1);
    expect(traffic).toHaveLength(1);
  });

  test('listAccountsRequest surfaces structured IPC errors', async () => {
    const listAccounts = vi.fn(async () => ({
      accounts: [],
      error: {
        code: 'linearizable_read_requires_leader',
        message: "Linearizable read for table 'accounts' requires cluster leader (leader site_id=2)",
      },
    }));
    setTightrope({ ...baselineApi, listAccounts });

    await expect(listAccountsRequest()).rejects.toThrow(
      "Linearizable read for table 'accounts' requires cluster leader (leader site_id=2)",
    );
  });

  test('refreshAccountUsageTelemetryRequest surfaces structured IPC errors', async () => {
    const refreshAccountUsageTelemetry = vi.fn(async () => ({
      error: {
        code: 'usage_refresh_failed',
        message: 'Failed to fetch usage telemetry from provider',
      },
    }));
    setTightrope({ ...baselineApi, refreshAccountUsageTelemetry });

    await expect(refreshAccountUsageTelemetryRequest('acc_1')).rejects.toThrow(
      'Failed to fetch usage telemetry from provider',
    );
  });

  test('refreshAccountTokenRequest surfaces structured IPC errors', async () => {
    const refreshAccountToken = vi.fn(async () => ({
      error: {
        code: 'token_refresh_failed',
        message: 'Failed to refresh account token',
      },
    }));
    setTightrope({ ...baselineApi, refreshAccountToken });

    await expect(refreshAccountTokenRequest('acc_1')).rejects.toThrow('Failed to refresh account token');
  });

  test('oauth status wrapper returns safe fallback for malformed payloads', async () => {
    const oauthStatus = vi.fn(async () => ({ broken: true }));
    setTightrope({ ...baselineApi, oauthStatus });

    const status = await oauthStatusRequest();
    expect(status.status).toBe('idle');
    expect(status.errorMessage).toBe('Malformed OAuth status payload');
  });

  test('backend wrappers coerce malformed enabled flags to false', async () => {
    const getBackendStatus = vi.fn(async () => ({ enabled: 'yes' }));
    const startBackend = vi.fn(async () => ({ enabled: 1 }));
    const stopBackend = vi.fn(async () => ({ enabled: null }));
    setTightrope({ ...baselineApi, getBackendStatus, startBackend, stopBackend });

    await expect(backendStatusRequest()).resolves.toEqual({ enabled: false });
    await expect(backendStartRequest()).resolves.toEqual({ enabled: false });
    await expect(backendStopRequest()).resolves.toEqual({ enabled: false });
  });

  test('account mutation wrappers throw for malformed account payloads', async () => {
    const importAccount = vi.fn(async () => ({ id: 'bad' }));
    const refreshAccountUsageTelemetry = vi.fn(async () => ({ id: 'bad' }));
    setTightrope({ ...baselineApi, importAccount, refreshAccountUsageTelemetry });

    await expect(importAccountRequest('one@test.local', 'openai')).rejects.toThrow('Malformed account payload');
    await expect(refreshAccountUsageTelemetryRequest('acc_1')).rejects.toThrow('Malformed account telemetry payload');
  });

  test('sql import wrappers coerce malformed payloads safely', async () => {
    const previewSqlImport = vi.fn(async () => ({
      source: { path: '/tmp/a.db', fileName: 'a.db', sizeBytes: 1, modifiedAtMs: 2, schemaFingerprint: 'accounts:id' },
      totals: { scanned: 1, newCount: 1, updateCount: 0, skipCount: 0, invalidCount: 0 },
      rows: [
        {
          sourceRowId: '1',
          dedupeKey: 'email_provider:a@test.local|openai',
          email: 'a@test.local',
          provider: 'openai',
          planType: 'plus',
          hasAccessToken: true,
          hasRefreshToken: true,
          hasIdToken: true,
          action: 'new',
          reason: 'No destination match.',
        },
      ],
      warnings: [],
    }));
    const applySqlImport = vi.fn(async () => ({
      totals: { scanned: 1, inserted: 1, updated: 0, skipped: 0, invalid: 0, failed: 0 },
      warnings: [],
    }));
    setTightrope({ ...baselineApi, previewSqlImport, applySqlImport });

    const preview = await previewSqlImportRequest({ sourcePath: '/tmp/a.db', importWithoutOverwrite: true });
    const apply = await applySqlImportRequest({ sourcePath: '/tmp/a.db', importWithoutOverwrite: true });

    expect(preview.rows).toHaveLength(1);
    expect(preview.rows[0]?.action).toBe('new');
    expect(apply.totals.inserted).toBe(1);
  });

  test('cluster status wrapper returns null for malformed payload', async () => {
    const getClusterStatus = vi.fn(async () => 'bad');
    setTightrope({ ...baselineApi, getClusterStatus });

    await expect(getClusterStatusRequest()).resolves.toBeNull();
  });

  test('event subscription wrappers return unsubscribe handlers when available', () => {
    const aboutUnsubscribe = vi.fn();
    const oauthUnsubscribe = vi.fn();
    const syncUnsubscribe = vi.fn();
    const oauthEvent: OauthDeepLinkEvent = { kind: 'success', url: 'tightrope://oauth/callback' };
    const syncEvent: SyncEvent = { type: 'term_change', ts: 1, term: 2 };
    const onAboutOpen = vi.fn((listener: () => void) => {
      listener();
      return aboutUnsubscribe;
    });
    const onOauthDeepLink = vi.fn((listener: (event: OauthDeepLinkEvent) => void) => {
      listener(oauthEvent);
      return oauthUnsubscribe;
    });
    const onSyncEvent = vi.fn((listener: (event: SyncEvent) => void) => {
      listener(syncEvent);
      return syncUnsubscribe;
    });
    setTightrope({ ...baselineApi, onAboutOpen, onOauthDeepLink, onSyncEvent });

    const aboutListener = vi.fn();
    const oauthListener = vi.fn();
    const syncListener = vi.fn();
    const aboutStop = onAboutOpenRequest(aboutListener);
    const oauthStop = onOauthDeepLinkRequest(oauthListener);
    const syncStop = onSyncEventRequest(syncListener);

    expect(aboutListener).toHaveBeenCalledTimes(1);
    expect(oauthListener).toHaveBeenCalledWith(oauthEvent);
    expect(syncListener).toHaveBeenCalledWith(syncEvent);
    aboutStop?.();
    oauthStop?.();
    syncStop?.();
    expect(aboutUnsubscribe).toHaveBeenCalledTimes(1);
    expect(oauthUnsubscribe).toHaveBeenCalledTimes(1);
    expect(syncUnsubscribe).toHaveBeenCalledTimes(1);
  });

  test('event subscription wrappers return null when hooks are unavailable', () => {
    setTightrope({} as ElectronApi);
    expect(onAboutOpenRequest(vi.fn())).toBeNull();
    expect(onOauthDeepLinkRequest(vi.fn())).toBeNull();
    expect(onSyncEventRequest(vi.fn())).toBeNull();
  });

  test('window wrappers delegate to platform and window controls', async () => {
    const windowClose = vi.fn(async () => {});
    const windowMinimize = vi.fn(async () => {});
    const windowToggleMaximize = vi.fn(async () => {});
    const windowIsMaximized = vi.fn(async () => true);
    setTightrope({
      ...baselineApi,
      platform: 'linux',
      windowClose,
      windowMinimize,
      windowToggleMaximize,
      windowIsMaximized,
    });

    expect(platformRequest()).toBe('linux');
    await expect(windowCloseRequest()).resolves.toBe(true);
    await expect(windowMinimizeRequest()).resolves.toBe(true);
    await expect(windowToggleMaximizeRequest()).resolves.toBe(true);
    await expect(windowIsMaximizedRequest()).resolves.toBe(true);
    expect(windowClose).toHaveBeenCalledTimes(1);
    expect(windowMinimize).toHaveBeenCalledTimes(1);
    expect(windowToggleMaximize).toHaveBeenCalledTimes(1);
    expect(windowIsMaximized).toHaveBeenCalledTimes(1);
  });

  test('window wrappers return safe fallbacks when controls are unavailable', async () => {
    setTightrope({} as ElectronApi);

    expect(platformRequest()).toBeNull();
    await expect(windowCloseRequest()).resolves.toBe(false);
    await expect(windowMinimizeRequest()).resolves.toBe(false);
    await expect(windowToggleMaximizeRequest()).resolves.toBe(false);
    await expect(windowIsMaximizedRequest()).resolves.toBeNull();
  });
});
