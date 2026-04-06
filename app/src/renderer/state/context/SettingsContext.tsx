import { createContext, useContext } from 'react';
import type {
  ClusterStatus,
  DashboardSettings,
  FirewallIpEntry,
  FirewallMode,
  RoutingMode,
  ScoringModel,
  SyncConflictResolution,
  ThemeMode,
  UpstreamStreamTransport,
} from '../../shared/types';

export interface SettingsContextValue {
  routingModes: RoutingMode[];
  routingMode: string;
  scoringModel: ScoringModel;
  theme: ThemeMode;
  dashboardSettings: DashboardSettings;
  firewallMode: FirewallMode;
  firewallEntries: FirewallIpEntry[];
  firewallDraftIpAddress: string;
  clusterStatus: ClusterStatus;
  manualPeerAddress: string;
  syncTopologyDialogOpen: boolean;
  settingsDirty: boolean;
  settingsSaving: boolean;
  setRoutingMode: (modeId: string) => void;
  setStrategyParam: (modeId: string, key: string, value: number) => void;
  setScoringWeight: (key: 'alpha' | 'beta' | 'gamma' | 'delta' | 'zeta' | 'eta', value: number) => void;
  setHeadroomWeight: (key: 'wp' | 'ws', value: number) => void;
  setUpstreamStreamTransport: (transport: UpstreamStreamTransport) => void;
  setStickyThreadsEnabled: (enabled: boolean) => void;
  setPreferEarlierResetAccounts: (enabled: boolean) => void;
  setStrictLockPoolContinuations: (enabled: boolean) => void;
  updateLockedRoutingAccountIds: (accountIds: string[]) => Promise<boolean>;
  setOpenaiCacheAffinityMaxAgeSeconds: (seconds: number) => void;
  setImportWithoutOverwrite: (enabled: boolean) => void;
  setRoutingPlanModelPricingUsdPerMillion: (value: string) => void;
  setFirewallDraftIpAddress: (value: string) => void;
  addFirewallIpAddress: () => Promise<void>;
  removeFirewallIpAddress: (ipAddress: string) => Promise<void>;
  toggleSyncEnabled: () => Promise<void>;
  setSyncSiteId: (siteId: number) => void;
  setSyncPort: (port: number) => void;
  setSyncDiscoveryEnabled: (enabled: boolean) => void;
  setSyncClusterName: (clusterName: string) => void;
  setManualPeerAddress: (value: string) => void;
  addManualPeer: () => Promise<void>;
  removePeer: (siteId: string) => Promise<void>;
  setSyncIntervalSeconds: (seconds: number) => void;
  setSyncConflictResolution: (strategy: SyncConflictResolution) => void;
  setSyncJournalRetentionDays: (days: number) => void;
  setSyncTlsEnabled: (enabled: boolean) => void;
  setSyncRequireHandshakeAuth: (enabled: boolean) => void;
  setSyncClusterSharedSecret: (secret: string) => void;
  setSyncTlsVerifyPeer: (enabled: boolean) => void;
  setSyncTlsCaCertificatePath: (path: string) => void;
  setSyncTlsCertificateChainPath: (path: string) => void;
  setSyncTlsPrivateKeyPath: (path: string) => void;
  setSyncTlsPinnedPeerCertificateSha256: (value: string) => void;
  setSyncSchemaVersion: (version: number) => void;
  setSyncMinSupportedSchemaVersion: (version: number) => void;
  setSyncAllowSchemaDowngrade: (enabled: boolean) => void;
  setSyncPeerProbeEnabled: (enabled: boolean) => void;
  setSyncPeerProbeIntervalMs: (value: number) => void;
  setSyncPeerProbeTimeoutMs: (value: number) => void;
  setSyncPeerProbeMaxPerRefresh: (value: number) => void;
  setSyncPeerProbeFailClosed: (enabled: boolean) => void;
  setSyncPeerProbeFailClosedFailures: (value: number) => void;
  triggerSyncNow: () => Promise<void>;
  openSyncTopology: () => void;
  closeSyncTopology: () => void;
  setTheme: (theme: ThemeMode) => void;
  saveSettings: () => void;
  discardSettings: () => void;
}

export const SettingsContext = createContext<SettingsContextValue | null>(null);

export function useSettingsContext(): SettingsContextValue {
  const context = useContext(SettingsContext);
  if (!context) {
    throw new Error('useSettingsContext must be used within AppStateProviders');
  }
  return context;
}
