import type { TightropeModelStateData } from './tightropeModelComposition';

interface BuildStateDataBundleInput {
  state: TightropeModelStateData['state'];
  accountsState: {
    accounts: TightropeModelStateData['accounts'];
    trafficClockMs: TightropeModelStateData['trafficClockMs'];
    trafficActiveWindowMs: TightropeModelStateData['trafficActiveWindowMs'];
  };
  settingsState: {
    routingModes: TightropeModelStateData['routingModes'];
    dashboardSettings: TightropeModelStateData['dashboardSettings'];
    hasUnsavedSettingsChanges: TightropeModelStateData['hasUnsavedSettingsChanges'];
    settingsSaveInFlight: TightropeModelStateData['settingsSaveInFlight'];
  };
  firewallState: {
    firewallMode: TightropeModelStateData['firewallMode'];
    firewallEntries: TightropeModelStateData['firewallEntries'];
    firewallDraftIpAddress: TightropeModelStateData['firewallDraftIpAddress'];
  };
  clusterSyncState: {
    clusterStatus: TightropeModelStateData['clusterStatus'];
    manualPeerAddress: TightropeModelStateData['manualPeerAddress'];
  };
  navigationState: {
    settingsLeaveDialogOpen: TightropeModelStateData['settingsLeaveDialogOpen'];
  };
  accountsView: {
    filteredAccounts: TightropeModelStateData['filteredAccounts'];
    selectedAccountDetail: TightropeModelStateData['selectedAccountDetail'];
    selectedAccountUsage24h: TightropeModelStateData['selectedAccountUsage24h'];
    isRefreshingSelectedAccountTelemetry: TightropeModelStateData['isRefreshingSelectedAccountTelemetry'];
    isRefreshingSelectedAccountToken: TightropeModelStateData['isRefreshingSelectedAccountToken'];
    isRefreshingAllAccountTelemetry: TightropeModelStateData['isRefreshingAllAccountTelemetry'];
  };
  utils: {
    stableSparklinePercents: TightropeModelStateData['stableSparklinePercents'];
    formatNumber: TightropeModelStateData['formatNumber'];
  };
}

export function buildStateDataBundle(input: BuildStateDataBundleInput): TightropeModelStateData {
  const {
    state,
    accountsState,
    settingsState,
    firewallState,
    clusterSyncState,
    navigationState,
    accountsView,
    utils,
  } = input;

  return {
    state,
    accounts: accountsState.accounts,
    routingModes: settingsState.routingModes,
    dashboardSettings: settingsState.dashboardSettings,
    firewallMode: firewallState.firewallMode,
    firewallEntries: firewallState.firewallEntries,
    firewallDraftIpAddress: firewallState.firewallDraftIpAddress,
    clusterStatus: clusterSyncState.clusterStatus,
    manualPeerAddress: clusterSyncState.manualPeerAddress,
    hasUnsavedSettingsChanges: settingsState.hasUnsavedSettingsChanges,
    settingsSaveInFlight: settingsState.settingsSaveInFlight,
    settingsLeaveDialogOpen: navigationState.settingsLeaveDialogOpen,
    filteredAccounts: accountsView.filteredAccounts,
    selectedAccountDetail: accountsView.selectedAccountDetail,
    selectedAccountUsage24h: accountsView.selectedAccountUsage24h,
    isRefreshingSelectedAccountTelemetry: accountsView.isRefreshingSelectedAccountTelemetry,
    isRefreshingSelectedAccountToken: accountsView.isRefreshingSelectedAccountToken,
    isRefreshingAllAccountTelemetry: accountsView.isRefreshingAllAccountTelemetry,
    trafficClockMs: accountsState.trafficClockMs,
    trafficActiveWindowMs: accountsState.trafficActiveWindowMs,
    stableSparklinePercents: utils.stableSparklinePercents,
    formatNumber: utils.formatNumber,
  };
}
