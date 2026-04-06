import type { DashboardSettings, DashboardSettingsUpdate } from '../../shared/types';

type DashboardSettingsPatchableKey = keyof DashboardSettings & keyof DashboardSettingsUpdate;

export const DASHBOARD_SETTINGS_UPDATE_KEYS: readonly DashboardSettingsPatchableKey[] = [
  'theme',
  'stickyThreadsEnabled',
  'upstreamStreamTransport',
  'preferEarlierResetAccounts',
  'routingStrategy',
  'strictLockPoolContinuations',
  'lockedRoutingAccountIds',
  'openaiCacheAffinityMaxAgeSeconds',
  'importWithoutOverwrite',
  'totpRequiredOnLogin',
  'apiKeyAuthEnabled',
  'routingHeadroomWeightPrimary',
  'routingHeadroomWeightSecondary',
  'routingScoreAlpha',
  'routingScoreBeta',
  'routingScoreGamma',
  'routingScoreDelta',
  'routingScoreZeta',
  'routingScoreEta',
  'routingSuccessRateRho',
  'routingPlanModelPricingUsdPerMillion',
  'syncClusterName',
  'syncSiteId',
  'syncPort',
  'syncDiscoveryEnabled',
  'syncIntervalSeconds',
  'syncConflictResolution',
  'syncJournalRetentionDays',
  'syncTlsEnabled',
  'syncRequireHandshakeAuth',
  'syncClusterSharedSecret',
  'syncTlsVerifyPeer',
  'syncTlsCaCertificatePath',
  'syncTlsCertificateChainPath',
  'syncTlsPrivateKeyPath',
  'syncTlsPinnedPeerCertificateSha256',
  'syncSchemaVersion',
  'syncMinSupportedSchemaVersion',
  'syncAllowSchemaDowngrade',
  'syncPeerProbeEnabled',
  'syncPeerProbeIntervalMs',
  'syncPeerProbeTimeoutMs',
  'syncPeerProbeMaxPerRefresh',
  'syncPeerProbeFailClosed',
  'syncPeerProbeFailClosedFailures',
] as const;

export const SYNC_SETTINGS_FINGERPRINT_KEYS: readonly (keyof DashboardSettings)[] = [
  'syncClusterName',
  'syncSiteId',
  'syncPort',
  'syncDiscoveryEnabled',
  'syncIntervalSeconds',
  'syncConflictResolution',
  'syncJournalRetentionDays',
  'syncTlsEnabled',
  'syncRequireHandshakeAuth',
  'syncClusterSharedSecret',
  'syncTlsVerifyPeer',
  'syncTlsCaCertificatePath',
  'syncTlsCertificateChainPath',
  'syncTlsPrivateKeyPath',
  'syncTlsPinnedPeerCertificateSha256',
  'syncSchemaVersion',
  'syncMinSupportedSchemaVersion',
  'syncAllowSchemaDowngrade',
  'syncPeerProbeEnabled',
  'syncPeerProbeIntervalMs',
  'syncPeerProbeTimeoutMs',
  'syncPeerProbeMaxPerRefresh',
  'syncPeerProbeFailClosed',
  'syncPeerProbeFailClosedFailures',
] as const;

function pickSettingsKeys<K extends keyof DashboardSettings>(
  settings: DashboardSettings,
  keys: readonly K[],
): Pick<DashboardSettings, K> {
  const subset = {} as Pick<DashboardSettings, K>;
  for (const key of keys) {
    subset[key] = settings[key];
  }
  return subset;
}

export function buildDashboardSettingsUpdate(settings: DashboardSettings): DashboardSettingsUpdate {
  return pickSettingsKeys(settings, DASHBOARD_SETTINGS_UPDATE_KEYS);
}

export function buildSyncSettingsFingerprint(settings: DashboardSettings): string {
  return JSON.stringify(pickSettingsKeys(settings, SYNC_SETTINGS_FINGERPRINT_KEYS));
}

export function boundedInteger(value: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, Math.trunc(value)));
}
