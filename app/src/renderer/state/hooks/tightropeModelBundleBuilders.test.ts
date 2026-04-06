import { describe, expect, test, vi } from 'vitest';
import { buildAccountActionsBundle, buildSettingsControls } from './tightropeModelBundleBuilders';

function sortedKeys(value: object): string[] {
  return Object.keys(value).sort();
}

describe('tightropeModelBundleBuilders', () => {
  test('buildSettingsControls wires settings/firewall/cluster/save handlers', () => {
    const setTheme = vi.fn();
    const triggerSyncNow = vi.fn();
    const saveSettingsChanges = vi.fn();
    const discardDashboardSettingsChanges = vi.fn();
    const settingsState = {
      setScoringWeight: vi.fn(),
      setHeadroomWeight: vi.fn(),
      setStrategyParam: vi.fn(),
      setUpstreamStreamTransport: vi.fn(),
      setStickyThreadsEnabled: vi.fn(),
      setPreferEarlierResetAccounts: vi.fn(),
      setStrictLockPoolContinuations: vi.fn(),
      updateLockedRoutingAccountIds: vi.fn(async () => true),
      setOpenaiCacheAffinityMaxAgeSeconds: vi.fn(),
      setImportWithoutOverwrite: vi.fn(),
      setRoutingPlanModelPricingUsdPerMillion: vi.fn(),
      setSyncSiteId: vi.fn(),
      setSyncPort: vi.fn(),
      setSyncDiscoveryEnabled: vi.fn(),
      setSyncClusterName: vi.fn(),
      setSyncIntervalSeconds: vi.fn(),
      setSyncConflictResolution: vi.fn(),
      setSyncJournalRetentionDays: vi.fn(),
      setSyncTlsEnabled: vi.fn(),
      setSyncRequireHandshakeAuth: vi.fn(),
      setSyncClusterSharedSecret: vi.fn(),
      setSyncTlsVerifyPeer: vi.fn(),
      setSyncTlsCaCertificatePath: vi.fn(),
      setSyncTlsCertificateChainPath: vi.fn(),
      setSyncTlsPrivateKeyPath: vi.fn(),
      setSyncTlsPinnedPeerCertificateSha256: vi.fn(),
      setSyncSchemaVersion: vi.fn(),
      setSyncMinSupportedSchemaVersion: vi.fn(),
      setSyncAllowSchemaDowngrade: vi.fn(),
      setSyncPeerProbeEnabled: vi.fn(),
      setSyncPeerProbeIntervalMs: vi.fn(),
      setSyncPeerProbeTimeoutMs: vi.fn(),
      setSyncPeerProbeMaxPerRefresh: vi.fn(),
      setSyncPeerProbeFailClosed: vi.fn(),
      setSyncPeerProbeFailClosedFailures: vi.fn(),
      setTheme,
    };
    const firewallState = {
      setFirewallDraft: vi.fn(),
      addFirewallIpAddress: vi.fn(),
      removeFirewallIpAddress: vi.fn(),
    };
    const clusterSyncState = {
      toggleSyncEnabled: vi.fn(),
      setManualPeer: vi.fn(),
      addManualPeer: vi.fn(),
      removeSyncPeer: vi.fn(),
      triggerSyncNow,
    };
    const settingsActions = {
      saveSettingsChanges,
      discardDashboardSettingsChanges,
    };

    const controls = buildSettingsControls({
      settingsState,
      firewallState,
      clusterSyncState,
      settingsActions,
    });

    expect(controls.setTheme).toBe(setTheme);
    expect(controls.triggerSyncNow).toBe(triggerSyncNow);
    expect(controls.saveSettingsChanges).toBe(saveSettingsChanges);
    expect(controls.discardDashboardSettingsChanges).toBe(discardDashboardSettingsChanges);
    expect(controls.setFirewallDraft).toBe(firewallState.setFirewallDraft);
    expect(controls.toggleSyncEnabled).toBe(clusterSyncState.toggleSyncEnabled);
    expect(sortedKeys(controls)).toEqual([
      'addFirewallIpAddress',
      'addManualPeer',
      'discardDashboardSettingsChanges',
      'removeFirewallIpAddress',
      'removeSyncPeer',
      'saveSettingsChanges',
      'setFirewallDraft',
      'setHeadroomWeight',
      'setImportWithoutOverwrite',
      'setManualPeer',
      'setOpenaiCacheAffinityMaxAgeSeconds',
      'setPreferEarlierResetAccounts',
      'setRoutingPlanModelPricingUsdPerMillion',
      'setScoringWeight',
      'setStickyThreadsEnabled',
      'setStrategyParam',
      'setStrictLockPoolContinuations',
      'setSyncAllowSchemaDowngrade',
      'setSyncClusterName',
      'setSyncClusterSharedSecret',
      'setSyncConflictResolution',
      'setSyncDiscoveryEnabled',
      'setSyncIntervalSeconds',
      'setSyncJournalRetentionDays',
      'setSyncMinSupportedSchemaVersion',
      'setSyncPeerProbeEnabled',
      'setSyncPeerProbeFailClosed',
      'setSyncPeerProbeFailClosedFailures',
      'setSyncPeerProbeIntervalMs',
      'setSyncPeerProbeMaxPerRefresh',
      'setSyncPeerProbeTimeoutMs',
      'setSyncPort',
      'setSyncRequireHandshakeAuth',
      'setSyncSchemaVersion',
      'setSyncSiteId',
      'setSyncTlsCaCertificatePath',
      'setSyncTlsCertificateChainPath',
      'setSyncTlsEnabled',
      'setSyncTlsPinnedPeerCertificateSha256',
      'setSyncTlsPrivateKeyPath',
      'setSyncTlsVerifyPeer',
      'setTheme',
      'setUpstreamStreamTransport',
      'toggleSyncEnabled',
      'triggerSyncNow',
      'updateLockedRoutingAccountIds',
    ]);
  });

  test('buildAccountActionsBundle wires oauth/ui/account action handlers', () => {
    const oauthState = {
      openAddAccountDialog: vi.fn(),
      closeAddAccountDialog: vi.fn(),
      setAddAccountStep: vi.fn(),
      selectImportFile: vi.fn(),
      submitImport: vi.fn(),
      simulateBrowserAuth: vi.fn(),
      submitManualCallback: vi.fn(),
      setManualCallback: vi.fn(),
      copyBrowserAuthUrl: vi.fn(),
      copyDeviceVerificationUrl: vi.fn(),
      startDeviceFlow: vi.fn(),
      cancelDeviceFlow: vi.fn(),
    };
    const uiState = {
      setAccountSearchQuery: vi.fn(),
      setAccountStatusFilter: vi.fn(),
      selectAccountDetail: vi.fn(),
    };
    const accountActions = {
      toggleAccountPin: vi.fn(),
      refreshSelectedAccountTelemetry: vi.fn(),
      refreshSelectedAccountToken: vi.fn(),
      refreshAllAccountsTelemetry: vi.fn(),
      pauseSelectedAccount: vi.fn(),
      reactivateSelectedAccount: vi.fn(),
      deleteSelectedAccount: vi.fn(),
    };

    const bundle = buildAccountActionsBundle({
      oauthState,
      uiState,
      accountActions,
    });

    expect(bundle.openAddAccountDialog).toBe(oauthState.openAddAccountDialog);
    expect(bundle.setAccountSearchQuery).toBe(uiState.setAccountSearchQuery);
    expect(bundle.toggleAccountPin).toBe(accountActions.toggleAccountPin);
    expect(bundle.refreshSelectedAccountToken).toBe(accountActions.refreshSelectedAccountToken);
    expect(bundle.deleteSelectedAccount).toBe(accountActions.deleteSelectedAccount);
    expect(sortedKeys(bundle)).toEqual([
      'cancelDeviceFlow',
      'closeAddAccountDialog',
      'copyBrowserAuthUrl',
      'copyDeviceVerificationUrl',
      'deleteSelectedAccount',
      'openAddAccountDialog',
      'pauseSelectedAccount',
      'reactivateSelectedAccount',
      'refreshAllAccountsTelemetry',
      'refreshSelectedAccountTelemetry',
      'refreshSelectedAccountToken',
      'selectAccountDetail',
      'selectImportFile',
      'setAccountSearchQuery',
      'setAccountStatusFilter',
      'setAddAccountStep',
      'setManualCallback',
      'simulateBrowserAuth',
      'startDeviceFlow',
      'submitImport',
      'submitManualCallback',
      'toggleAccountPin',
    ]);
  });
});
