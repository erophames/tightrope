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
import { AppearanceSection } from './sections/AppearanceSection';
import { DatabaseSyncSection } from './sections/DatabaseSyncSection';
import { FirewallSection } from './sections/FirewallSection';
import { RoutingOptionsSection } from './sections/RoutingOptionsSection';
import { RoutingStrategySection } from './sections/RoutingStrategySection';

interface SettingsPageProps {
  visible: boolean;
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
  onSetRoutingMode: (modeId: string) => void;
  onSetStrategyParam: (modeId: string, key: string, value: number) => void;
  onSetScoringWeight: (key: 'alpha' | 'beta' | 'gamma' | 'delta' | 'zeta' | 'eta', value: number) => void;
  onSetHeadroomWeight: (key: 'wp' | 'ws', value: number) => void;
  onSetUpstreamStreamTransport: (transport: UpstreamStreamTransport) => void;
  onSetStickyThreadsEnabled: (enabled: boolean) => void;
  onSetPreferEarlierResetAccounts: (enabled: boolean) => void;
  onSetOpenaiCacheAffinityMaxAgeSeconds: (seconds: number) => void;
  onSetRoutingPlanModelPricingUsdPerMillion: (value: string) => void;
  onSetFirewallDraftIpAddress: (value: string) => void;
  onAddFirewallIpAddress: () => void;
  onRemoveFirewallIpAddress: (ipAddress: string) => void;
  onToggleSyncEnabled: () => void;
  onSetSyncSiteId: (siteId: number) => void;
  onSetSyncPort: (port: number) => void;
  onSetSyncDiscoveryEnabled: (enabled: boolean) => void;
  onSetSyncClusterName: (clusterName: string) => void;
  onSetManualPeerAddress: (value: string) => void;
  onAddManualPeer: () => void;
  onRemovePeer: (siteId: string) => void;
  onSetSyncIntervalSeconds: (seconds: number) => void;
  onSetSyncConflictResolution: (strategy: SyncConflictResolution) => void;
  onSetSyncJournalRetentionDays: (days: number) => void;
  onSetSyncTlsEnabled: (enabled: boolean) => void;
  onSetSyncRequireHandshakeAuth: (enabled: boolean) => void;
  onSetSyncClusterSharedSecret: (secret: string) => void;
  onSetSyncTlsVerifyPeer: (enabled: boolean) => void;
  onSetSyncTlsCaCertificatePath: (path: string) => void;
  onSetSyncTlsCertificateChainPath: (path: string) => void;
  onSetSyncTlsPrivateKeyPath: (path: string) => void;
  onSetSyncTlsPinnedPeerCertificateSha256: (value: string) => void;
  onSetSyncSchemaVersion: (version: number) => void;
  onSetSyncMinSupportedSchemaVersion: (version: number) => void;
  onSetSyncAllowSchemaDowngrade: (enabled: boolean) => void;
  onSetSyncPeerProbeEnabled: (enabled: boolean) => void;
  onSetSyncPeerProbeIntervalMs: (value: number) => void;
  onSetSyncPeerProbeTimeoutMs: (value: number) => void;
  onSetSyncPeerProbeMaxPerRefresh: (value: number) => void;
  onSetSyncPeerProbeFailClosed: (enabled: boolean) => void;
  onSetSyncPeerProbeFailClosedFailures: (value: number) => void;
  onTriggerSyncNow: () => void;
  onSetTheme: (theme: ThemeMode) => void;
}

export function SettingsPage({
  visible,
  routingModes,
  routingMode,
  scoringModel,
  theme,
  dashboardSettings,
  firewallMode,
  firewallEntries,
  firewallDraftIpAddress,
  clusterStatus,
  manualPeerAddress,
  onSetRoutingMode,
  onSetStrategyParam,
  onSetScoringWeight,
  onSetHeadroomWeight,
  onSetUpstreamStreamTransport,
  onSetStickyThreadsEnabled,
  onSetPreferEarlierResetAccounts,
  onSetOpenaiCacheAffinityMaxAgeSeconds,
  onSetRoutingPlanModelPricingUsdPerMillion,
  onSetFirewallDraftIpAddress,
  onAddFirewallIpAddress,
  onRemoveFirewallIpAddress,
  onToggleSyncEnabled,
  onSetSyncSiteId,
  onSetSyncPort,
  onSetSyncDiscoveryEnabled,
  onSetSyncClusterName,
  onSetManualPeerAddress,
  onAddManualPeer,
  onRemovePeer,
  onSetSyncIntervalSeconds,
  onSetSyncConflictResolution,
  onSetSyncJournalRetentionDays,
  onSetSyncTlsEnabled,
  onSetSyncRequireHandshakeAuth,
  onSetSyncClusterSharedSecret,
  onSetSyncTlsVerifyPeer,
  onSetSyncTlsCaCertificatePath,
  onSetSyncTlsCertificateChainPath,
  onSetSyncTlsPrivateKeyPath,
  onSetSyncTlsPinnedPeerCertificateSha256,
  onSetSyncSchemaVersion,
  onSetSyncMinSupportedSchemaVersion,
  onSetSyncAllowSchemaDowngrade,
  onSetSyncPeerProbeEnabled,
  onSetSyncPeerProbeIntervalMs,
  onSetSyncPeerProbeTimeoutMs,
  onSetSyncPeerProbeMaxPerRefresh,
  onSetSyncPeerProbeFailClosed,
  onSetSyncPeerProbeFailClosedFailures,
  onTriggerSyncNow,
  onSetTheme,
}: SettingsPageProps) {
  if (!visible) return null;

  return (
    <section className="settings-page page active" id="pageSettings" data-page="settings">
      <div className="settings-scroll">
        <header className="section-header">
          <div>
            <p className="eyebrow">Configuration</p>
            <h2>Settings</h2>
          </div>
        </header>
        <div className="settings-body">
          <RoutingStrategySection
            routingModes={routingModes}
            routingMode={routingMode}
            scoringModel={scoringModel}
            onSetRoutingMode={onSetRoutingMode}
            onSetStrategyParam={onSetStrategyParam}
            onSetScoringWeight={onSetScoringWeight}
            onSetHeadroomWeight={onSetHeadroomWeight}
          />
          <RoutingOptionsSection
            upstreamStreamTransport={dashboardSettings.upstreamStreamTransport}
            stickyThreadsEnabled={dashboardSettings.stickyThreadsEnabled}
            preferEarlierResetAccounts={dashboardSettings.preferEarlierResetAccounts}
            openaiCacheAffinityMaxAgeSeconds={dashboardSettings.openaiCacheAffinityMaxAgeSeconds}
            routingPlanModelPricingUsdPerMillion={dashboardSettings.routingPlanModelPricingUsdPerMillion}
            onSetUpstreamStreamTransport={onSetUpstreamStreamTransport}
            onSetStickyThreadsEnabled={onSetStickyThreadsEnabled}
            onSetPreferEarlierResetAccounts={onSetPreferEarlierResetAccounts}
            onSetOpenaiCacheAffinityMaxAgeSeconds={onSetOpenaiCacheAffinityMaxAgeSeconds}
            onSetRoutingPlanModelPricingUsdPerMillion={onSetRoutingPlanModelPricingUsdPerMillion}
          />
          <FirewallSection
            mode={firewallMode}
            entries={firewallEntries}
            draftIpAddress={firewallDraftIpAddress}
            onSetDraftIpAddress={onSetFirewallDraftIpAddress}
            onAddIpAddress={onAddFirewallIpAddress}
            onRemoveIpAddress={onRemoveFirewallIpAddress}
          />
          <DatabaseSyncSection
            syncEnabled={clusterStatus.enabled}
            syncSiteId={dashboardSettings.syncSiteId}
            syncPort={dashboardSettings.syncPort}
            syncDiscoveryEnabled={dashboardSettings.syncDiscoveryEnabled}
            syncClusterName={dashboardSettings.syncClusterName}
            manualPeerAddress={manualPeerAddress}
            syncIntervalSeconds={dashboardSettings.syncIntervalSeconds}
            syncConflictResolution={dashboardSettings.syncConflictResolution}
            syncJournalRetentionDays={dashboardSettings.syncJournalRetentionDays}
            syncTlsEnabled={dashboardSettings.syncTlsEnabled}
            syncRequireHandshakeAuth={dashboardSettings.syncRequireHandshakeAuth}
            syncClusterSharedSecret={dashboardSettings.syncClusterSharedSecret}
            syncTlsVerifyPeer={dashboardSettings.syncTlsVerifyPeer}
            syncTlsCaCertificatePath={dashboardSettings.syncTlsCaCertificatePath}
            syncTlsCertificateChainPath={dashboardSettings.syncTlsCertificateChainPath}
            syncTlsPrivateKeyPath={dashboardSettings.syncTlsPrivateKeyPath}
            syncTlsPinnedPeerCertificateSha256={dashboardSettings.syncTlsPinnedPeerCertificateSha256}
            syncSchemaVersion={dashboardSettings.syncSchemaVersion}
            syncMinSupportedSchemaVersion={dashboardSettings.syncMinSupportedSchemaVersion}
            syncAllowSchemaDowngrade={dashboardSettings.syncAllowSchemaDowngrade}
            syncPeerProbeEnabled={dashboardSettings.syncPeerProbeEnabled}
            syncPeerProbeIntervalMs={dashboardSettings.syncPeerProbeIntervalMs}
            syncPeerProbeTimeoutMs={dashboardSettings.syncPeerProbeTimeoutMs}
            syncPeerProbeMaxPerRefresh={dashboardSettings.syncPeerProbeMaxPerRefresh}
            syncPeerProbeFailClosed={dashboardSettings.syncPeerProbeFailClosed}
            syncPeerProbeFailClosedFailures={dashboardSettings.syncPeerProbeFailClosedFailures}
            clusterStatus={clusterStatus}
            onToggleSyncEnabled={onToggleSyncEnabled}
            onSetSyncSiteId={onSetSyncSiteId}
            onSetSyncPort={onSetSyncPort}
            onSetSyncDiscoveryEnabled={onSetSyncDiscoveryEnabled}
            onSetSyncClusterName={onSetSyncClusterName}
            onSetManualPeerAddress={onSetManualPeerAddress}
            onAddManualPeer={onAddManualPeer}
            onRemovePeer={onRemovePeer}
            onSetSyncIntervalSeconds={onSetSyncIntervalSeconds}
            onSetSyncConflictResolution={onSetSyncConflictResolution}
            onSetSyncJournalRetentionDays={onSetSyncJournalRetentionDays}
            onSetSyncTlsEnabled={onSetSyncTlsEnabled}
            onSetSyncRequireHandshakeAuth={onSetSyncRequireHandshakeAuth}
            onSetSyncClusterSharedSecret={onSetSyncClusterSharedSecret}
            onSetSyncTlsVerifyPeer={onSetSyncTlsVerifyPeer}
            onSetSyncTlsCaCertificatePath={onSetSyncTlsCaCertificatePath}
            onSetSyncTlsCertificateChainPath={onSetSyncTlsCertificateChainPath}
            onSetSyncTlsPrivateKeyPath={onSetSyncTlsPrivateKeyPath}
            onSetSyncTlsPinnedPeerCertificateSha256={onSetSyncTlsPinnedPeerCertificateSha256}
            onSetSyncSchemaVersion={onSetSyncSchemaVersion}
            onSetSyncMinSupportedSchemaVersion={onSetSyncMinSupportedSchemaVersion}
            onSetSyncAllowSchemaDowngrade={onSetSyncAllowSchemaDowngrade}
            onSetSyncPeerProbeEnabled={onSetSyncPeerProbeEnabled}
            onSetSyncPeerProbeIntervalMs={onSetSyncPeerProbeIntervalMs}
            onSetSyncPeerProbeTimeoutMs={onSetSyncPeerProbeTimeoutMs}
            onSetSyncPeerProbeMaxPerRefresh={onSetSyncPeerProbeMaxPerRefresh}
            onSetSyncPeerProbeFailClosed={onSetSyncPeerProbeFailClosed}
            onSetSyncPeerProbeFailClosedFailures={onSetSyncPeerProbeFailClosedFailures}
            onTriggerSyncNow={onTriggerSyncNow}
          />
          <AppearanceSection theme={theme} onSetTheme={onSetTheme} />
        </div>
      </div>
    </section>
  );
}
