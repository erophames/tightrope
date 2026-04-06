import type { TightropeModel } from '../context/modelTypes';

type TightropeModelStateRuntimeKeys = 'state' | 'accounts';

type TightropeModelStateSettingsSnapshotKeys =
  | 'routingModes'
  | 'dashboardSettings'
  | 'firewallMode'
  | 'firewallEntries'
  | 'firewallDraftIpAddress'
  | 'clusterStatus'
  | 'manualPeerAddress'
  | 'hasUnsavedSettingsChanges'
  | 'settingsSaveInFlight'
  | 'settingsLeaveDialogOpen';

type TightropeModelStateAccountViewKeys =
  | 'filteredAccounts'
  | 'selectedAccountDetail'
  | 'selectedAccountUsage24h'
  | 'isRefreshingSelectedAccountTelemetry'
  | 'isRefreshingSelectedAccountToken'
  | 'isRefreshingAllAccountTelemetry';

type TightropeModelStateTrafficKeys =
  | 'trafficClockMs'
  | 'trafficActiveWindowMs'
  | 'stableSparklinePercents'
  | 'formatNumber';

type TightropeModelStateDataKeys =
  | TightropeModelStateRuntimeKeys
  | TightropeModelStateSettingsSnapshotKeys
  | TightropeModelStateAccountViewKeys
  | TightropeModelStateTrafficKeys;

type TightropeModelNavigationKeys =
  | 'setCurrentPage'
  | 'closeSettingsLeaveDialog'
  | 'discardSettingsAndNavigate'
  | 'saveSettingsAndNavigate';

type TightropeModelRouterKeys = 'setSearchQuery' | 'setSelectedAccountId' | 'setSelectedRoute' | 'setInspectorOpen';

type TightropeModelRuntimeBackendKeys = 'setRuntimeAction' | 'toggleRoutePause' | 'toggleAutoRestart';

type TightropeModelRuntimeAuthListenerKeys =
  | 'setRoutingMode'
  | 'createListenerUrl'
  | 'toggleListener'
  | 'restartListener'
  | 'initAuth0'
  | 'captureAuthResponse';

type TightropeModelRuntimeKeys = TightropeModelRuntimeBackendKeys | TightropeModelRuntimeAuthListenerKeys;

type TightropeModelSettingsRoutingKeys =
  | 'setScoringWeight'
  | 'setHeadroomWeight'
  | 'setStrategyParam'
  | 'setUpstreamStreamTransport'
  | 'setStickyThreadsEnabled'
  | 'setPreferEarlierResetAccounts'
  | 'setStrictLockPoolContinuations'
  | 'updateLockedRoutingAccountIds'
  | 'setOpenaiCacheAffinityMaxAgeSeconds'
  | 'setImportWithoutOverwrite'
  | 'setRoutingPlanModelPricingUsdPerMillion';

type TightropeModelSettingsFirewallKeys =
  | 'setFirewallDraft'
  | 'addFirewallIpAddress'
  | 'removeFirewallIpAddress';

type TightropeModelSettingsSyncKeys =
  | 'toggleSyncEnabled'
  | 'setSyncSiteId'
  | 'setSyncPort'
  | 'setSyncDiscoveryEnabled'
  | 'setSyncClusterName'
  | 'setManualPeer'
  | 'addManualPeer'
  | 'removeSyncPeer'
  | 'setSyncIntervalSeconds'
  | 'setSyncConflictResolution'
  | 'setSyncJournalRetentionDays'
  | 'setSyncTlsEnabled'
  | 'setSyncRequireHandshakeAuth'
  | 'setSyncClusterSharedSecret'
  | 'setSyncTlsVerifyPeer'
  | 'setSyncTlsCaCertificatePath'
  | 'setSyncTlsCertificateChainPath'
  | 'setSyncTlsPrivateKeyPath'
  | 'setSyncTlsPinnedPeerCertificateSha256'
  | 'setSyncSchemaVersion'
  | 'setSyncMinSupportedSchemaVersion'
  | 'setSyncAllowSchemaDowngrade'
  | 'setSyncPeerProbeEnabled'
  | 'setSyncPeerProbeIntervalMs'
  | 'setSyncPeerProbeTimeoutMs'
  | 'setSyncPeerProbeMaxPerRefresh'
  | 'setSyncPeerProbeFailClosed'
  | 'setSyncPeerProbeFailClosedFailures'
  | 'triggerSyncNow';

type TightropeModelSettingsLifecycleKeys = 'saveSettingsChanges' | 'discardDashboardSettingsChanges';

type TightropeModelSettingsAppearanceKeys = 'setTheme';

type TightropeModelSettingsKeys =
  | TightropeModelSettingsRoutingKeys
  | TightropeModelSettingsFirewallKeys
  | TightropeModelSettingsSyncKeys
  | TightropeModelSettingsLifecycleKeys
  | TightropeModelSettingsAppearanceKeys;

type TightropeModelDialogKeys =
  | 'openBackendDialog'
  | 'closeBackendDialog'
  | 'openAuthDialog'
  | 'closeAuthDialog'
  | 'openSyncTopologyDialog'
  | 'closeSyncTopologyDialog';

type TightropeModelAccountOauthKeys =
  | 'openAddAccountDialog'
  | 'closeAddAccountDialog'
  | 'setAddAccountStep'
  | 'selectImportFile'
  | 'submitImport'
  | 'simulateBrowserAuth'
  | 'submitManualCallback'
  | 'setManualCallback'
  | 'copyBrowserAuthUrl'
  | 'copyDeviceVerificationUrl'
  | 'startDeviceFlow'
  | 'cancelDeviceFlow';

type TightropeModelAccountFilterAndSelectionKeys =
  | 'setAccountSearchQuery'
  | 'setAccountStatusFilter'
  | 'selectAccountDetail';

type TightropeModelAccountRuntimeActionsKeys =
  | 'toggleAccountPin'
  | 'refreshSelectedAccountTelemetry'
  | 'refreshSelectedAccountToken'
  | 'refreshAllAccountsTelemetry'
  | 'pauseSelectedAccount'
  | 'reactivateSelectedAccount'
  | 'deleteSelectedAccount';

type TightropeModelAccountKeys =
  | TightropeModelAccountOauthKeys
  | TightropeModelAccountFilterAndSelectionKeys
  | TightropeModelAccountRuntimeActionsKeys;

type TightropeModelSessionsKeys =
  | 'setSessionsKindFilter'
  | 'prevSessionsPage'
  | 'nextSessionsPage'
  | 'purgeStaleSessions';

type TightropeModelLogsKeys = 'openDrawer' | 'closeDrawer';

type TightropeModelSessionsAndLogsKeys = TightropeModelSessionsKeys | TightropeModelLogsKeys;

export type TightropeModelStateData = Pick<TightropeModel, TightropeModelStateDataKeys>;
export type TightropeModelNavigationActions = Pick<TightropeModel, TightropeModelNavigationKeys>;
export type TightropeModelRouterActions = Pick<TightropeModel, TightropeModelRouterKeys>;
export type TightropeModelRuntimeActions = Pick<TightropeModel, TightropeModelRuntimeKeys>;
export type TightropeModelSettingsControls = Pick<TightropeModel, TightropeModelSettingsKeys>;
export type TightropeModelDialogActions = Pick<TightropeModel, TightropeModelDialogKeys>;
export type TightropeModelAccountActionsBundle = Pick<TightropeModel, TightropeModelAccountKeys>;
export type TightropeModelSessionsAndLogsActions = Pick<TightropeModel, TightropeModelSessionsAndLogsKeys>;

interface BuildTightropeModelInput {
  stateData: TightropeModelStateData;
  navigationActions: TightropeModelNavigationActions;
  routerActions: TightropeModelRouterActions;
  runtimeActions: TightropeModelRuntimeActions;
  settingsControls: TightropeModelSettingsControls;
  dialogActions: TightropeModelDialogActions;
  accountActionsBundle: TightropeModelAccountActionsBundle;
  sessionsAndLogsActions: TightropeModelSessionsAndLogsActions;
}

export function buildTightropeModel(input: BuildTightropeModelInput): TightropeModel {
  const {
    stateData,
    navigationActions,
    routerActions,
    runtimeActions,
    settingsControls,
    dialogActions,
    accountActionsBundle,
    sessionsAndLogsActions,
  } = input;

  return {
    ...stateData,
    ...navigationActions,
    ...routerActions,
    ...runtimeActions,
    ...settingsControls,
    ...dialogActions,
    ...accountActionsBundle,
    ...sessionsAndLogsActions,
  } satisfies TightropeModel;
}
