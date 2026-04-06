import { describe, expect, test, vi } from 'vitest';
import { buildBootstrapOptions } from './bootstrapOptionsBuilder';

function sortedKeys(value: object): string[] {
  return Object.keys(value).sort();
}

describe('bootstrapOptionsBuilder', () => {
  test('buildBootstrapOptions wires bootstrap refresh dependencies', () => {
    const accountsState = {
      refreshAccountsFromNative: vi.fn(async () => {}),
      refreshAccountTrafficSnapshot: vi.fn(async () => {}),
    };
    const runtimeDomain = {
      refreshBackendState: vi.fn(async () => {}),
    };
    const sessionsState = {
      refreshStickySessions: vi.fn(async () => {}),
    };
    const requestLogState = {
      refreshRequestLogs: vi.fn(async () => {}),
    };
    const settingsState = {
      refreshDashboardSettingsFromNative: vi.fn(async () => {}),
    };
    const firewallState = {
      refreshFirewallIps: vi.fn(async () => {}),
    };
    const clusterSyncState = {
      refreshClusterState: vi.fn(async () => {}),
    };
    const oauthState = {
      bootstrapOauthState: vi.fn(async () => {}),
    };
    const pushRuntimeEvent = vi.fn();

    const options = buildBootstrapOptions({
      accountsState,
      runtimeDomain,
      sessionsState,
      requestLogState,
      settingsState,
      firewallState,
      clusterSyncState,
      oauthState,
      pushRuntimeEvent,
    });

    expect(options.refreshAccountsFromNative).toBe(accountsState.refreshAccountsFromNative);
    expect(options.refreshBackendState).toBe(runtimeDomain.refreshBackendState);
    expect(options.refreshAccountTrafficSnapshot).toBe(accountsState.refreshAccountTrafficSnapshot);
    expect(options.refreshStickySessions).toBe(sessionsState.refreshStickySessions);
    expect(options.refreshRequestLogs).toBe(requestLogState.refreshRequestLogs);
    expect(options.refreshDashboardSettingsFromNative).toBe(settingsState.refreshDashboardSettingsFromNative);
    expect(options.refreshFirewallIps).toBe(firewallState.refreshFirewallIps);
    expect(options.refreshClusterState).toBe(clusterSyncState.refreshClusterState);
    expect(options.bootstrapOauthState).toBe(oauthState.bootstrapOauthState);
    expect(options.pushRuntimeEvent).toBe(pushRuntimeEvent);

    expect(sortedKeys(options)).toEqual([
      'bootstrapOauthState',
      'pushRuntimeEvent',
      'refreshAccountTrafficSnapshot',
      'refreshAccountsFromNative',
      'refreshBackendState',
      'refreshClusterState',
      'refreshDashboardSettingsFromNative',
      'refreshFirewallIps',
      'refreshRequestLogs',
      'refreshStickySessions',
    ]);
  });
});
