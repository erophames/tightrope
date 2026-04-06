import { describe, expect, test, vi } from 'vitest';
import { createInitialRuntimeState } from '../../test/fixtures/seed';
import type { TightropeModelStateData } from './tightropeModelComposition';
import { buildStateDataBundle } from './tightropeModelStateDataBuilder';

function sortedKeys(value: object): string[] {
  return Object.keys(value).sort();
}

describe('tightropeModelStateDataBuilder', () => {
  test('buildStateDataBundle wires domain slices and utility helpers', () => {
    const state = createInitialRuntimeState();
    const stableSparklinePercents = vi.fn();
    const formatNumber = vi.fn();
    const accounts = [] as TightropeModelStateData['accounts'];
    const routingModes = [] as TightropeModelStateData['routingModes'];
    const dashboardSettings = {} as TightropeModelStateData['dashboardSettings'];
    const firewallMode = 'allow_all' as TightropeModelStateData['firewallMode'];
    const firewallEntries: TightropeModelStateData['firewallEntries'] = [];
    const clusterStatus = {} as TightropeModelStateData['clusterStatus'];
    const filteredAccounts: TightropeModelStateData['filteredAccounts'] = [];
    const selectedAccountUsage24h: TightropeModelStateData['selectedAccountUsage24h'] = {
      requests: 1,
      tokens: 2,
      costUsd: 3,
      failovers: 4,
    };

    const stateData = buildStateDataBundle({
      state,
      accountsState: {
        accounts,
        trafficClockMs: 42,
        trafficActiveWindowMs: 84,
      },
      settingsState: {
        routingModes,
        dashboardSettings,
        hasUnsavedSettingsChanges: true,
        settingsSaveInFlight: true,
      },
      firewallState: {
        firewallMode,
        firewallEntries,
        firewallDraftIpAddress: '10.0.0.1',
      },
      clusterSyncState: {
        clusterStatus,
        manualPeerAddress: '127.0.0.1:4040',
      },
      navigationState: {
        settingsLeaveDialogOpen: true,
      },
      accountsView: {
        filteredAccounts,
        selectedAccountDetail: null,
        selectedAccountUsage24h,
        isRefreshingAllAccountTelemetry: false,
        isRefreshingSelectedAccountTelemetry: true,
        isRefreshingSelectedAccountToken: false,
      },
      utils: {
        stableSparklinePercents,
        formatNumber,
      },
    });

    expect(stateData.state).toBe(state);
    expect(stateData.accounts).toBe(accounts);
    expect(stateData.routingModes).toBe(routingModes);
    expect(stateData.dashboardSettings).toBe(dashboardSettings);
    expect(stateData.firewallMode).toBe(firewallMode);
    expect(stateData.firewallEntries).toBe(firewallEntries);
    expect(stateData.clusterStatus).toBe(clusterStatus);
    expect(stateData.settingsLeaveDialogOpen).toBe(true);
    expect(stateData.filteredAccounts).toBe(filteredAccounts);
    expect(stateData.selectedAccountUsage24h).toBe(selectedAccountUsage24h);
    expect(stateData.stableSparklinePercents).toBe(stableSparklinePercents);
    expect(stateData.formatNumber).toBe(formatNumber);
    expect(stateData.trafficClockMs).toBe(42);
    expect(stateData.trafficActiveWindowMs).toBe(84);
    expect(sortedKeys(stateData)).toEqual([
      'accounts',
      'clusterStatus',
      'dashboardSettings',
      'filteredAccounts',
      'firewallDraftIpAddress',
      'firewallEntries',
      'firewallMode',
      'formatNumber',
      'hasUnsavedSettingsChanges',
      'isRefreshingAllAccountTelemetry',
      'isRefreshingSelectedAccountTelemetry',
      'isRefreshingSelectedAccountToken',
      'manualPeerAddress',
      'routingModes',
      'selectedAccountDetail',
      'selectedAccountUsage24h',
      'settingsLeaveDialogOpen',
      'settingsSaveInFlight',
      'stableSparklinePercents',
      'state',
      'trafficActiveWindowMs',
      'trafficClockMs',
    ]);
  });
});
