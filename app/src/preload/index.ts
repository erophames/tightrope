// Minimal preload API — exposes safe IPC methods to the renderer
import { contextBridge, ipcRenderer } from 'electron';
import { isIP } from 'node:net';

function rejectValidation(message: string): Promise<never> {
  return Promise.reject(new Error(message));
}

function parsePort(portText: string): number | null {
  if (!/^\d{1,5}$/.test(portText)) {
    return null;
  }
  const port = Number(portText);
  if (!Number.isInteger(port) || port < 1 || port > 65535) {
    return null;
  }
  return port;
}

function isValidIpv4(address: string): boolean {
  const parts = address.split('.');
  if (parts.length !== 4) {
    return false;
  }
  return parts.every((part) => {
    if (!/^\d{1,3}$/.test(part)) {
      return false;
    }
    const value = Number(part);
    return value >= 0 && value <= 255;
  });
}

function isValidHostname(host: string): boolean {
  if (host.length > 253) {
    return false;
  }
  const labels = host.split('.');
  if (labels.some((label) => label.length === 0 || label.length > 63)) {
    return false;
  }
  return labels.every((label) => /^[A-Za-z0-9](?:[A-Za-z0-9-]*[A-Za-z0-9])?$/.test(label));
}

function isValidFirewallIp(input: string): boolean {
  const value = input.trim();
  if (!value) {
    return false;
  }
  const slash = value.indexOf('/');
  if (slash < 0) {
    return isIP(value) !== 0;
  }
  if (value.indexOf('/', slash + 1) !== -1) {
    return false;
  }
  const address = value.slice(0, slash);
  const prefixText = value.slice(slash + 1);
  if (!/^\d{1,3}$/.test(prefixText)) {
    return false;
  }
  const family = isIP(address);
  if (family === 0) {
    return false;
  }
  const prefix = Number(prefixText);
  const maxPrefix = family === 4 ? 32 : 128;
  return prefix >= 0 && prefix <= maxPrefix;
}

function isValidPeerAddress(input: string): boolean {
  const value = input.trim();
  if (!value) {
    return false;
  }

  if (value.startsWith('[')) {
    const closingIndex = value.indexOf(']');
    if (closingIndex <= 1 || closingIndex + 1 >= value.length || value[closingIndex + 1] !== ':') {
      return false;
    }
    const host = value.slice(1, closingIndex);
    const portText = value.slice(closingIndex + 2);
    return isIP(host) === 6 && parsePort(portText) !== null;
  }

  const colonIndex = value.lastIndexOf(':');
  if (colonIndex <= 0 || colonIndex === value.length - 1 || value.indexOf(':') !== colonIndex) {
    return false;
  }
  const host = value.slice(0, colonIndex);
  const portText = value.slice(colonIndex + 1);
  if (parsePort(portText) === null) {
    return false;
  }

  return host === 'localhost' || isValidIpv4(host) || isValidHostname(host);
}

function isValidSiteId(input: string): boolean {
  return /^[1-9]\d*$/.test(input.trim());
}

contextBridge.exposeInMainWorld('tightrope', {
  platform: process.platform,
  getHealth: () => ipcRenderer.invoke('health'),
  getSettings: () => ipcRenderer.invoke('settings:get'),
  updateSettings: (update: unknown) => ipcRenderer.invoke('settings:update', update),
  oauthStart: (payload?: unknown) => ipcRenderer.invoke('oauth:start', payload),
  oauthStatus: () => ipcRenderer.invoke('oauth:status'),
  oauthStop: () => ipcRenderer.invoke('oauth:stop'),
  oauthRestart: () => ipcRenderer.invoke('oauth:restart'),
  oauthComplete: (payload: unknown) => ipcRenderer.invoke('oauth:complete', payload),
  oauthManualCallback: (callbackUrl: string) => ipcRenderer.invoke('oauth:manual-callback', callbackUrl),
  onOauthDeepLink: (listener: (event: { kind: 'success' | 'callback'; url: string }) => void) => {
    const handler = (_event: unknown, payload: unknown) => {
      if (!payload || typeof payload !== 'object') {
        return;
      }
      const maybeKind = (payload as { kind?: unknown }).kind;
      const maybeUrl = (payload as { url?: unknown }).url;
      if ((maybeKind === 'success' || maybeKind === 'callback') && typeof maybeUrl === 'string') {
        listener({ kind: maybeKind, url: maybeUrl });
      }
    };
    ipcRenderer.on('oauth:deep-link', handler);
    return () => {
      ipcRenderer.removeListener('oauth:deep-link', handler);
    };
  },
  listAccounts: () => ipcRenderer.invoke('accounts:list'),
  listRequestLogs: (payload?: { limit?: number; offset?: number }) => ipcRenderer.invoke('logs:list', payload),
  listAccountTraffic: () => ipcRenderer.invoke('accounts:traffic'),
  importAccount: (payload: unknown) => ipcRenderer.invoke('accounts:import', payload),
  pauseAccount: (accountId: string) => ipcRenderer.invoke('accounts:pause', accountId),
  reactivateAccount: (accountId: string) => ipcRenderer.invoke('accounts:reactivate', accountId),
  deleteAccount: (accountId: string) => ipcRenderer.invoke('accounts:delete', accountId),
  refreshAccountUsageTelemetry: (accountId: string) => ipcRenderer.invoke('accounts:refresh-usage', accountId),
  listFirewallIps: () => ipcRenderer.invoke('firewall:list'),
  addFirewallIp: (ipAddress: string) => {
    if (!isValidFirewallIp(ipAddress)) {
      return rejectValidation('Invalid IP/CIDR address');
    }
    return ipcRenderer.invoke('firewall:add', ipAddress.trim());
  },
  removeFirewallIp: (ipAddress: string) => ipcRenderer.invoke('firewall:remove', ipAddress),
  clusterEnable: (config: unknown) => ipcRenderer.invoke('cluster:enable', config),
  clusterDisable: () => ipcRenderer.invoke('cluster:disable'),
  getClusterStatus: () => ipcRenderer.invoke('cluster:status'),
  addPeer: (address: string) => {
    if (!isValidPeerAddress(address)) {
      return rejectValidation('Invalid peer address; expected host:port or [ipv6]:port');
    }
    return ipcRenderer.invoke('cluster:add-peer', address.trim());
  },
  removePeer: (siteId: string) => {
    if (!isValidSiteId(siteId)) {
      return rejectValidation('Invalid peer site ID');
    }
    return ipcRenderer.invoke('cluster:remove-peer', siteId.trim());
  },
  triggerSync: () => ipcRenderer.invoke('sync:trigger'),
  rollbackSyncBatch: (batchId: string) => ipcRenderer.invoke('sync:rollback-batch', batchId),
  windowMinimize: () => ipcRenderer.invoke('window:minimize'),
  windowToggleMaximize: () => ipcRenderer.invoke('window:toggle-maximize'),
  windowClose: () => ipcRenderer.invoke('window:close'),
  windowIsMaximized: () => ipcRenderer.invoke('window:is-maximized'),
});
