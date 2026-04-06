import type {
  TightropeModelDialogActions,
  TightropeModelNavigationActions,
  TightropeModelRouterActions,
  TightropeModelRuntimeActions,
  TightropeModelSessionsAndLogsActions,
} from './tightropeModelComposition';

interface BuildNavigationActionsInput {
  navigationState: {
    setCurrentPage: TightropeModelNavigationActions['setCurrentPage'];
    closeSettingsLeaveDialog: TightropeModelNavigationActions['closeSettingsLeaveDialog'];
    discardSettingsAndNavigate: TightropeModelNavigationActions['discardSettingsAndNavigate'];
    saveSettingsAndNavigate: TightropeModelNavigationActions['saveSettingsAndNavigate'];
  };
}

export function buildNavigationActions(input: BuildNavigationActionsInput): TightropeModelNavigationActions {
  const { navigationState } = input;
  return {
    setCurrentPage: navigationState.setCurrentPage,
    closeSettingsLeaveDialog: navigationState.closeSettingsLeaveDialog,
    discardSettingsAndNavigate: navigationState.discardSettingsAndNavigate,
    saveSettingsAndNavigate: navigationState.saveSettingsAndNavigate,
  };
}

interface BuildRouterActionsInput {
  uiState: {
    setSearchQuery: TightropeModelRouterActions['setSearchQuery'];
    setSelectedAccountId: TightropeModelRouterActions['setSelectedAccountId'];
    setSelectedRoute: TightropeModelRouterActions['setSelectedRoute'];
    setInspectorOpen: TightropeModelRouterActions['setInspectorOpen'];
  };
}

export function buildRouterActions(input: BuildRouterActionsInput): TightropeModelRouterActions {
  const { uiState } = input;
  return {
    setSearchQuery: uiState.setSearchQuery,
    setSelectedAccountId: uiState.setSelectedAccountId,
    setSelectedRoute: uiState.setSelectedRoute,
    setInspectorOpen: uiState.setInspectorOpen,
  };
}

interface BuildRuntimeActionsInput {
  settingsState: {
    setRoutingMode: TightropeModelRuntimeActions['setRoutingMode'];
  };
  runtimeDomain: {
    setRuntimeAction: TightropeModelRuntimeActions['setRuntimeAction'];
    toggleRoutePause: TightropeModelRuntimeActions['toggleRoutePause'];
    toggleAutoRestart: TightropeModelRuntimeActions['toggleAutoRestart'];
  };
  oauthState: {
    createListenerUrl: TightropeModelRuntimeActions['createListenerUrl'];
    toggleListener: TightropeModelRuntimeActions['toggleListener'];
    restartListener: TightropeModelRuntimeActions['restartListener'];
    initAuth0: TightropeModelRuntimeActions['initAuth0'];
    captureAuthResponse: TightropeModelRuntimeActions['captureAuthResponse'];
  };
}

export function buildRuntimeActions(input: BuildRuntimeActionsInput): TightropeModelRuntimeActions {
  const { settingsState, runtimeDomain, oauthState } = input;
  return {
    setRoutingMode: settingsState.setRoutingMode,
    setRuntimeAction: runtimeDomain.setRuntimeAction,
    toggleRoutePause: runtimeDomain.toggleRoutePause,
    toggleAutoRestart: runtimeDomain.toggleAutoRestart,
    createListenerUrl: oauthState.createListenerUrl,
    toggleListener: oauthState.toggleListener,
    restartListener: oauthState.restartListener,
    initAuth0: oauthState.initAuth0,
    captureAuthResponse: oauthState.captureAuthResponse,
  };
}

interface BuildDialogActionsInput {
  uiState: {
    openBackendDialog: TightropeModelDialogActions['openBackendDialog'];
    closeBackendDialog: TightropeModelDialogActions['closeBackendDialog'];
    openAuthDialog: TightropeModelDialogActions['openAuthDialog'];
    closeAuthDialog: TightropeModelDialogActions['closeAuthDialog'];
    openSyncTopologyDialog: TightropeModelDialogActions['openSyncTopologyDialog'];
    closeSyncTopologyDialog: TightropeModelDialogActions['closeSyncTopologyDialog'];
  };
}

export function buildDialogActions(input: BuildDialogActionsInput): TightropeModelDialogActions {
  const { uiState } = input;
  return {
    openBackendDialog: uiState.openBackendDialog,
    closeBackendDialog: uiState.closeBackendDialog,
    openAuthDialog: uiState.openAuthDialog,
    closeAuthDialog: uiState.closeAuthDialog,
    openSyncTopologyDialog: uiState.openSyncTopologyDialog,
    closeSyncTopologyDialog: uiState.closeSyncTopologyDialog,
  };
}

interface BuildSessionsAndLogsActionsInput {
  uiState: {
    setSessionsKindFilter: TightropeModelSessionsAndLogsActions['setSessionsKindFilter'];
    prevSessionsPage: TightropeModelSessionsAndLogsActions['prevSessionsPage'];
    nextSessionsPage: TightropeModelSessionsAndLogsActions['nextSessionsPage'];
    purgeStaleSessions: TightropeModelSessionsAndLogsActions['purgeStaleSessions'];
    openDrawer: TightropeModelSessionsAndLogsActions['openDrawer'];
    closeDrawer: TightropeModelSessionsAndLogsActions['closeDrawer'];
  };
}

export function buildSessionsAndLogsActions(
  input: BuildSessionsAndLogsActionsInput,
): TightropeModelSessionsAndLogsActions {
  const { uiState } = input;
  return {
    setSessionsKindFilter: uiState.setSessionsKindFilter,
    prevSessionsPage: uiState.prevSessionsPage,
    nextSessionsPage: uiState.nextSessionsPage,
    purgeStaleSessions: uiState.purgeStaleSessions,
    openDrawer: uiState.openDrawer,
    closeDrawer: uiState.closeDrawer,
  };
}
