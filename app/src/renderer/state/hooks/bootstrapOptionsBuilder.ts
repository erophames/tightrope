import type { UseBootstrapOptions } from './useBootstrap';

interface BuildBootstrapOptionsInput {
  accountsState: {
    refreshAccountsFromNative: UseBootstrapOptions['refreshAccountsFromNative'];
    refreshAccountTrafficSnapshot: UseBootstrapOptions['refreshAccountTrafficSnapshot'];
  };
  runtimeDomain: {
    refreshBackendState: UseBootstrapOptions['refreshBackendState'];
  };
  sessionsState: {
    refreshStickySessions: UseBootstrapOptions['refreshStickySessions'];
  };
  requestLogState: {
    refreshRequestLogs: UseBootstrapOptions['refreshRequestLogs'];
  };
  settingsState: {
    refreshDashboardSettingsFromNative: UseBootstrapOptions['refreshDashboardSettingsFromNative'];
  };
  firewallState: {
    refreshFirewallIps: UseBootstrapOptions['refreshFirewallIps'];
  };
  clusterSyncState: {
    refreshClusterState: UseBootstrapOptions['refreshClusterState'];
  };
  oauthState: {
    bootstrapOauthState: UseBootstrapOptions['bootstrapOauthState'];
  };
  pushRuntimeEvent: UseBootstrapOptions['pushRuntimeEvent'];
}

export function buildBootstrapOptions(input: BuildBootstrapOptionsInput): UseBootstrapOptions {
  return {
    refreshAccountsFromNative: input.accountsState.refreshAccountsFromNative,
    refreshBackendState: input.runtimeDomain.refreshBackendState,
    refreshAccountTrafficSnapshot: input.accountsState.refreshAccountTrafficSnapshot,
    refreshStickySessions: input.sessionsState.refreshStickySessions,
    refreshRequestLogs: input.requestLogState.refreshRequestLogs,
    refreshDashboardSettingsFromNative: input.settingsState.refreshDashboardSettingsFromNative,
    refreshFirewallIps: input.firewallState.refreshFirewallIps,
    refreshClusterState: input.clusterSyncState.refreshClusterState,
    bootstrapOauthState: input.oauthState.bootstrapOauthState,
    pushRuntimeEvent: input.pushRuntimeEvent,
  };
}
