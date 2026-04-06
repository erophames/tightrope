import type {
  TightropeModelAccountActionsBundle,
  TightropeModelSettingsControls,
} from './tightropeModelComposition';

interface BuildSettingsControlsInput {
  settingsState: {
    setScoringWeight: TightropeModelSettingsControls['setScoringWeight'];
    setHeadroomWeight: TightropeModelSettingsControls['setHeadroomWeight'];
    setStrategyParam: TightropeModelSettingsControls['setStrategyParam'];
    setUpstreamStreamTransport: TightropeModelSettingsControls['setUpstreamStreamTransport'];
    setStickyThreadsEnabled: TightropeModelSettingsControls['setStickyThreadsEnabled'];
    setPreferEarlierResetAccounts: TightropeModelSettingsControls['setPreferEarlierResetAccounts'];
    setStrictLockPoolContinuations: TightropeModelSettingsControls['setStrictLockPoolContinuations'];
    updateLockedRoutingAccountIds: TightropeModelSettingsControls['updateLockedRoutingAccountIds'];
    setOpenaiCacheAffinityMaxAgeSeconds: TightropeModelSettingsControls['setOpenaiCacheAffinityMaxAgeSeconds'];
    setImportWithoutOverwrite: TightropeModelSettingsControls['setImportWithoutOverwrite'];
    setRoutingPlanModelPricingUsdPerMillion: TightropeModelSettingsControls['setRoutingPlanModelPricingUsdPerMillion'];
    setSyncSiteId: TightropeModelSettingsControls['setSyncSiteId'];
    setSyncPort: TightropeModelSettingsControls['setSyncPort'];
    setSyncDiscoveryEnabled: TightropeModelSettingsControls['setSyncDiscoveryEnabled'];
    setSyncClusterName: TightropeModelSettingsControls['setSyncClusterName'];
    setSyncIntervalSeconds: TightropeModelSettingsControls['setSyncIntervalSeconds'];
    setSyncConflictResolution: TightropeModelSettingsControls['setSyncConflictResolution'];
    setSyncJournalRetentionDays: TightropeModelSettingsControls['setSyncJournalRetentionDays'];
    setSyncTlsEnabled: TightropeModelSettingsControls['setSyncTlsEnabled'];
    setSyncRequireHandshakeAuth: TightropeModelSettingsControls['setSyncRequireHandshakeAuth'];
    setSyncClusterSharedSecret: TightropeModelSettingsControls['setSyncClusterSharedSecret'];
    setSyncTlsVerifyPeer: TightropeModelSettingsControls['setSyncTlsVerifyPeer'];
    setSyncTlsCaCertificatePath: TightropeModelSettingsControls['setSyncTlsCaCertificatePath'];
    setSyncTlsCertificateChainPath: TightropeModelSettingsControls['setSyncTlsCertificateChainPath'];
    setSyncTlsPrivateKeyPath: TightropeModelSettingsControls['setSyncTlsPrivateKeyPath'];
    setSyncTlsPinnedPeerCertificateSha256: TightropeModelSettingsControls['setSyncTlsPinnedPeerCertificateSha256'];
    setSyncSchemaVersion: TightropeModelSettingsControls['setSyncSchemaVersion'];
    setSyncMinSupportedSchemaVersion: TightropeModelSettingsControls['setSyncMinSupportedSchemaVersion'];
    setSyncAllowSchemaDowngrade: TightropeModelSettingsControls['setSyncAllowSchemaDowngrade'];
    setSyncPeerProbeEnabled: TightropeModelSettingsControls['setSyncPeerProbeEnabled'];
    setSyncPeerProbeIntervalMs: TightropeModelSettingsControls['setSyncPeerProbeIntervalMs'];
    setSyncPeerProbeTimeoutMs: TightropeModelSettingsControls['setSyncPeerProbeTimeoutMs'];
    setSyncPeerProbeMaxPerRefresh: TightropeModelSettingsControls['setSyncPeerProbeMaxPerRefresh'];
    setSyncPeerProbeFailClosed: TightropeModelSettingsControls['setSyncPeerProbeFailClosed'];
    setSyncPeerProbeFailClosedFailures: TightropeModelSettingsControls['setSyncPeerProbeFailClosedFailures'];
    setTheme: TightropeModelSettingsControls['setTheme'];
  };
  firewallState: {
    setFirewallDraft: TightropeModelSettingsControls['setFirewallDraft'];
    addFirewallIpAddress: TightropeModelSettingsControls['addFirewallIpAddress'];
    removeFirewallIpAddress: TightropeModelSettingsControls['removeFirewallIpAddress'];
  };
  clusterSyncState: {
    toggleSyncEnabled: TightropeModelSettingsControls['toggleSyncEnabled'];
    setManualPeer: TightropeModelSettingsControls['setManualPeer'];
    addManualPeer: TightropeModelSettingsControls['addManualPeer'];
    removeSyncPeer: TightropeModelSettingsControls['removeSyncPeer'];
    triggerSyncNow: TightropeModelSettingsControls['triggerSyncNow'];
  };
  settingsActions: {
    saveSettingsChanges: TightropeModelSettingsControls['saveSettingsChanges'];
    discardDashboardSettingsChanges: TightropeModelSettingsControls['discardDashboardSettingsChanges'];
  };
}

export function buildSettingsControls(input: BuildSettingsControlsInput): TightropeModelSettingsControls {
  const { settingsState, firewallState, clusterSyncState, settingsActions } = input;
  return {
    setScoringWeight: settingsState.setScoringWeight,
    setHeadroomWeight: settingsState.setHeadroomWeight,
    setStrategyParam: settingsState.setStrategyParam,
    setUpstreamStreamTransport: settingsState.setUpstreamStreamTransport,
    setStickyThreadsEnabled: settingsState.setStickyThreadsEnabled,
    setPreferEarlierResetAccounts: settingsState.setPreferEarlierResetAccounts,
    setStrictLockPoolContinuations: settingsState.setStrictLockPoolContinuations,
    updateLockedRoutingAccountIds: settingsState.updateLockedRoutingAccountIds,
    setOpenaiCacheAffinityMaxAgeSeconds: settingsState.setOpenaiCacheAffinityMaxAgeSeconds,
    setImportWithoutOverwrite: settingsState.setImportWithoutOverwrite,
    setRoutingPlanModelPricingUsdPerMillion: settingsState.setRoutingPlanModelPricingUsdPerMillion,
    setFirewallDraft: firewallState.setFirewallDraft,
    addFirewallIpAddress: firewallState.addFirewallIpAddress,
    removeFirewallIpAddress: firewallState.removeFirewallIpAddress,
    toggleSyncEnabled: clusterSyncState.toggleSyncEnabled,
    setSyncSiteId: settingsState.setSyncSiteId,
    setSyncPort: settingsState.setSyncPort,
    setSyncDiscoveryEnabled: settingsState.setSyncDiscoveryEnabled,
    setSyncClusterName: settingsState.setSyncClusterName,
    setManualPeer: clusterSyncState.setManualPeer,
    addManualPeer: clusterSyncState.addManualPeer,
    removeSyncPeer: clusterSyncState.removeSyncPeer,
    setSyncIntervalSeconds: settingsState.setSyncIntervalSeconds,
    setSyncConflictResolution: settingsState.setSyncConflictResolution,
    setSyncJournalRetentionDays: settingsState.setSyncJournalRetentionDays,
    setSyncTlsEnabled: settingsState.setSyncTlsEnabled,
    setSyncRequireHandshakeAuth: settingsState.setSyncRequireHandshakeAuth,
    setSyncClusterSharedSecret: settingsState.setSyncClusterSharedSecret,
    setSyncTlsVerifyPeer: settingsState.setSyncTlsVerifyPeer,
    setSyncTlsCaCertificatePath: settingsState.setSyncTlsCaCertificatePath,
    setSyncTlsCertificateChainPath: settingsState.setSyncTlsCertificateChainPath,
    setSyncTlsPrivateKeyPath: settingsState.setSyncTlsPrivateKeyPath,
    setSyncTlsPinnedPeerCertificateSha256: settingsState.setSyncTlsPinnedPeerCertificateSha256,
    setSyncSchemaVersion: settingsState.setSyncSchemaVersion,
    setSyncMinSupportedSchemaVersion: settingsState.setSyncMinSupportedSchemaVersion,
    setSyncAllowSchemaDowngrade: settingsState.setSyncAllowSchemaDowngrade,
    setSyncPeerProbeEnabled: settingsState.setSyncPeerProbeEnabled,
    setSyncPeerProbeIntervalMs: settingsState.setSyncPeerProbeIntervalMs,
    setSyncPeerProbeTimeoutMs: settingsState.setSyncPeerProbeTimeoutMs,
    setSyncPeerProbeMaxPerRefresh: settingsState.setSyncPeerProbeMaxPerRefresh,
    setSyncPeerProbeFailClosed: settingsState.setSyncPeerProbeFailClosed,
    setSyncPeerProbeFailClosedFailures: settingsState.setSyncPeerProbeFailClosedFailures,
    saveSettingsChanges: settingsActions.saveSettingsChanges,
    discardDashboardSettingsChanges: settingsActions.discardDashboardSettingsChanges,
    triggerSyncNow: clusterSyncState.triggerSyncNow,
    setTheme: settingsState.setTheme,
  };
}

interface BuildAccountActionsBundleInput {
  oauthState: {
    openAddAccountDialog: TightropeModelAccountActionsBundle['openAddAccountDialog'];
    closeAddAccountDialog: TightropeModelAccountActionsBundle['closeAddAccountDialog'];
    setAddAccountStep: TightropeModelAccountActionsBundle['setAddAccountStep'];
    selectImportFile: TightropeModelAccountActionsBundle['selectImportFile'];
    submitImport: TightropeModelAccountActionsBundle['submitImport'];
    simulateBrowserAuth: TightropeModelAccountActionsBundle['simulateBrowserAuth'];
    submitManualCallback: TightropeModelAccountActionsBundle['submitManualCallback'];
    setManualCallback: TightropeModelAccountActionsBundle['setManualCallback'];
    copyBrowserAuthUrl: TightropeModelAccountActionsBundle['copyBrowserAuthUrl'];
    copyDeviceVerificationUrl: TightropeModelAccountActionsBundle['copyDeviceVerificationUrl'];
    startDeviceFlow: TightropeModelAccountActionsBundle['startDeviceFlow'];
    cancelDeviceFlow: TightropeModelAccountActionsBundle['cancelDeviceFlow'];
  };
  uiState: {
    setAccountSearchQuery: TightropeModelAccountActionsBundle['setAccountSearchQuery'];
    setAccountStatusFilter: TightropeModelAccountActionsBundle['setAccountStatusFilter'];
    selectAccountDetail: TightropeModelAccountActionsBundle['selectAccountDetail'];
  };
  accountActions: {
    toggleAccountPin: TightropeModelAccountActionsBundle['toggleAccountPin'];
    refreshSelectedAccountTelemetry: TightropeModelAccountActionsBundle['refreshSelectedAccountTelemetry'];
    refreshSelectedAccountToken: TightropeModelAccountActionsBundle['refreshSelectedAccountToken'];
    refreshAllAccountsTelemetry: TightropeModelAccountActionsBundle['refreshAllAccountsTelemetry'];
    pauseSelectedAccount: TightropeModelAccountActionsBundle['pauseSelectedAccount'];
    reactivateSelectedAccount: TightropeModelAccountActionsBundle['reactivateSelectedAccount'];
    deleteSelectedAccount: TightropeModelAccountActionsBundle['deleteSelectedAccount'];
  };
}

export function buildAccountActionsBundle(input: BuildAccountActionsBundleInput): TightropeModelAccountActionsBundle {
  const { oauthState, uiState, accountActions } = input;
  return {
    openAddAccountDialog: oauthState.openAddAccountDialog,
    closeAddAccountDialog: oauthState.closeAddAccountDialog,
    setAddAccountStep: oauthState.setAddAccountStep,
    selectImportFile: oauthState.selectImportFile,
    submitImport: oauthState.submitImport,
    simulateBrowserAuth: oauthState.simulateBrowserAuth,
    submitManualCallback: oauthState.submitManualCallback,
    setManualCallback: oauthState.setManualCallback,
    copyBrowserAuthUrl: oauthState.copyBrowserAuthUrl,
    copyDeviceVerificationUrl: oauthState.copyDeviceVerificationUrl,
    startDeviceFlow: oauthState.startDeviceFlow,
    cancelDeviceFlow: oauthState.cancelDeviceFlow,
    setAccountSearchQuery: uiState.setAccountSearchQuery,
    setAccountStatusFilter: uiState.setAccountStatusFilter,
    selectAccountDetail: uiState.selectAccountDetail,
    toggleAccountPin: accountActions.toggleAccountPin,
    refreshSelectedAccountTelemetry: accountActions.refreshSelectedAccountTelemetry,
    refreshSelectedAccountToken: accountActions.refreshSelectedAccountToken,
    refreshAllAccountsTelemetry: accountActions.refreshAllAccountsTelemetry,
    pauseSelectedAccount: accountActions.pauseSelectedAccount,
    reactivateSelectedAccount: accountActions.reactivateSelectedAccount,
    deleteSelectedAccount: accountActions.deleteSelectedAccount,
  };
}
