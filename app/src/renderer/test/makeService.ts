import { createTightropeService, type TightropeService } from '../services/tightrope';

interface LegacyServiceOverrides {
  listAccounts?: (...args: any[]) => Promise<{ accounts?: unknown }>;
  listStickySessions?: (...args: any[]) => Promise<unknown>;
  listRequestLogs?: (...args: any[]) => Promise<{ logs?: unknown }>;
  listAccountTraffic?: (...args: any[]) => Promise<{ accounts?: unknown }>;
  importAccount?: (...args: any[]) => Promise<any>;
  pinAccount?: (...args: any[]) => Promise<any>;
  unpinAccount?: (...args: any[]) => Promise<any>;
  pauseAccount?: (...args: any[]) => Promise<any>;
  reactivateAccount?: (...args: any[]) => Promise<any>;
  deleteAccount?: (...args: any[]) => Promise<any>;
  refreshAccountUsageTelemetry?: (...args: any[]) => Promise<any>;
  refreshAccountToken?: (...args: any[]) => Promise<any>;
  oauthStart?: (...args: any[]) => Promise<any>;
  oauthStatus?: (...args: any[]) => Promise<any>;
  oauthStop?: (...args: any[]) => Promise<any>;
  oauthRestart?: (...args: any[]) => Promise<any>;
  oauthManualCallback?: (...args: any[]) => Promise<any>;
  onOauthDeepLink?: (...args: any[]) => any;
  onAboutOpen?: (...args: any[]) => any;
  onSyncEvent?: (...args: any[]) => any;
  getSettings?: (...args: any[]) => Promise<any>;
  updateSettings?: (...args: any[]) => Promise<any>;
  listFirewallIps?: (...args: any[]) => Promise<any>;
  addFirewallIp?: (...args: any[]) => Promise<any>;
  removeFirewallIp?: (...args: any[]) => Promise<any>;
  getClusterStatus?: (...args: any[]) => Promise<any>;
  clusterEnable?: (...args: any[]) => Promise<any>;
  clusterDisable?: (...args: any[]) => Promise<any>;
  addPeer?: (...args: any[]) => Promise<any>;
  removePeer?: (...args: any[]) => Promise<any>;
  triggerSync?: (...args: any[]) => Promise<any>;
}

export type TestServiceOverrides = Partial<TightropeService> & LegacyServiceOverrides;

export function makeTestService(overrides: TestServiceOverrides = {}): TightropeService {
  const service: TightropeService = {
    ...createTightropeService(),
    ...overrides,
  };

  if (overrides.listAccounts) {
    service.listAccountsRequest = async () => {
      const response = await overrides.listAccounts?.();
      return Array.isArray(response?.accounts) ? (response.accounts as any[]) : [];
    };
  }
  if (overrides.listStickySessions) {
    service.listStickySessionsRequest = async (limit, offset) =>
      (await overrides.listStickySessions?.({ limit, offset })) as any;
  }
  if (overrides.listRequestLogs) {
    service.listRequestLogsRequest = async (limit, offset) => {
      const response = await overrides.listRequestLogs?.({ limit, offset });
      return Array.isArray(response?.logs) ? (response.logs as any[]) : [];
    };
  }
  if (overrides.listAccountTraffic) {
    service.listAccountTrafficRequest = async () => {
      const response = await overrides.listAccountTraffic?.();
      return Array.isArray(response?.accounts) ? (response.accounts as any[]) : [];
    };
  }
  if (overrides.importAccount) {
    service.importAccountRequest = async (email, provider) => overrides.importAccount?.({ email, provider });
  }
  if (overrides.pinAccount) {
    service.pinAccountRequest = async (accountId) => {
      await overrides.pinAccount?.(accountId);
    };
  }
  if (overrides.unpinAccount) {
    service.unpinAccountRequest = async (accountId) => {
      await overrides.unpinAccount?.(accountId);
    };
  }
  if (overrides.pauseAccount) {
    service.pauseAccountRequest = async (accountId) => {
      await overrides.pauseAccount?.(accountId);
    };
  }
  if (overrides.reactivateAccount) {
    service.reactivateAccountRequest = async (accountId) => {
      await overrides.reactivateAccount?.(accountId);
    };
  }
  if (overrides.deleteAccount) {
    service.deleteAccountRequest = async (accountId) => {
      await overrides.deleteAccount?.(accountId);
    };
  }
  if (overrides.refreshAccountUsageTelemetry) {
    service.refreshAccountUsageTelemetryRequest = async (accountId) => overrides.refreshAccountUsageTelemetry?.(accountId);
  }
  if (overrides.refreshAccountToken) {
    service.refreshAccountTokenRequest = async (accountId) => overrides.refreshAccountToken?.(accountId);
  }
  if (overrides.oauthStart) {
    service.oauthStartRequest = async (forceMethod) => overrides.oauthStart?.({ forceMethod });
  }
  if (overrides.oauthStatus) {
    service.oauthStatusRequest = async () => overrides.oauthStatus?.();
  }
  if (overrides.oauthStop) {
    service.oauthStopRequest = async () => overrides.oauthStop?.();
  }
  if (overrides.oauthRestart) {
    service.oauthRestartRequest = async () => overrides.oauthRestart?.();
  }
  if (overrides.oauthManualCallback) {
    service.oauthManualCallbackRequest = async (callbackUrl) => overrides.oauthManualCallback?.(callbackUrl);
  }
  if (overrides.onOauthDeepLink) {
    service.onOauthDeepLinkRequest = (listener) => overrides.onOauthDeepLink?.(listener) ?? null;
  }
  if (overrides.onAboutOpen) {
    service.onAboutOpenRequest = (listener) => overrides.onAboutOpen?.(listener) ?? null;
  }
  if (overrides.onSyncEvent) {
    service.onSyncEventRequest = (listener) => overrides.onSyncEvent?.(listener) ?? null;
  }
  if (overrides.getSettings) {
    service.getSettingsRequest = async () => overrides.getSettings?.();
  }
  if (overrides.updateSettings) {
    service.updateSettingsRequest = async (update) => overrides.updateSettings?.(update);
  }
  if (overrides.listFirewallIps) {
    service.listFirewallIpsRequest = async () => overrides.listFirewallIps?.();
  }
  if (overrides.addFirewallIp) {
    service.addFirewallIpRequest = async (ipAddress) => {
      await overrides.addFirewallIp?.(ipAddress);
      return true;
    };
  }
  if (overrides.removeFirewallIp) {
    service.removeFirewallIpRequest = async (ipAddress) => {
      await overrides.removeFirewallIp?.(ipAddress);
      return true;
    };
  }
  if (overrides.getClusterStatus) {
    service.getClusterStatusRequest = async () => overrides.getClusterStatus?.();
  }
  if (overrides.clusterEnable) {
    service.clusterEnableRequest = async (config) => {
      await overrides.clusterEnable?.(config);
      return true;
    };
  }
  if (overrides.clusterDisable) {
    service.clusterDisableRequest = async () => {
      await overrides.clusterDisable?.();
      return true;
    };
  }
  if (overrides.addPeer) {
    service.addPeerRequest = async (address) => {
      await overrides.addPeer?.(address);
      return true;
    };
  }
  if (overrides.removePeer) {
    service.removePeerRequest = async (siteId) => {
      await overrides.removePeer?.(siteId);
      return true;
    };
  }
  if (overrides.triggerSync) {
    service.triggerSyncRequest = async () => {
      await overrides.triggerSync?.();
      return true;
    };
  }

  return service;
}
