import { describe, expect, test, vi } from 'vitest';
import {
  buildDialogActions,
  buildNavigationActions,
  buildRouterActions,
  buildRuntimeActions,
  buildSessionsAndLogsActions,
} from './tightropeModelActionBuilders';

function sortedKeys(value: object): string[] {
  return Object.keys(value).sort();
}

describe('tightropeModelActionBuilders', () => {
  test('buildNavigationActions wires navigation handlers', () => {
    const navigationState = {
      setCurrentPage: vi.fn(),
      closeSettingsLeaveDialog: vi.fn(),
      discardSettingsAndNavigate: vi.fn(),
      saveSettingsAndNavigate: vi.fn(),
    };

    const actions = buildNavigationActions({ navigationState });

    expect(actions.setCurrentPage).toBe(navigationState.setCurrentPage);
    expect(actions.closeSettingsLeaveDialog).toBe(navigationState.closeSettingsLeaveDialog);
    expect(actions.discardSettingsAndNavigate).toBe(navigationState.discardSettingsAndNavigate);
    expect(actions.saveSettingsAndNavigate).toBe(navigationState.saveSettingsAndNavigate);
    expect(sortedKeys(actions)).toEqual([
      'closeSettingsLeaveDialog',
      'discardSettingsAndNavigate',
      'saveSettingsAndNavigate',
      'setCurrentPage',
    ]);
  });

  test('buildRouterActions wires router handlers', () => {
    const uiState = {
      setSearchQuery: vi.fn(),
      setSelectedAccountId: vi.fn(),
      setSelectedRoute: vi.fn(),
      setInspectorOpen: vi.fn(),
    };

    const actions = buildRouterActions({ uiState });

    expect(actions.setSearchQuery).toBe(uiState.setSearchQuery);
    expect(actions.setSelectedAccountId).toBe(uiState.setSelectedAccountId);
    expect(actions.setSelectedRoute).toBe(uiState.setSelectedRoute);
    expect(actions.setInspectorOpen).toBe(uiState.setInspectorOpen);
    expect(sortedKeys(actions)).toEqual([
      'setInspectorOpen',
      'setSearchQuery',
      'setSelectedAccountId',
      'setSelectedRoute',
    ]);
  });

  test('buildRuntimeActions wires settings/runtime/oauth handlers', () => {
    const settingsState = {
      setRoutingMode: vi.fn(),
    };
    const runtimeDomain = {
      setRuntimeAction: vi.fn(),
      toggleRoutePause: vi.fn(),
      toggleAutoRestart: vi.fn(),
    };
    const oauthState = {
      createListenerUrl: vi.fn(),
      toggleListener: vi.fn(),
      restartListener: vi.fn(),
      initAuth0: vi.fn(),
      captureAuthResponse: vi.fn(),
    };

    const actions = buildRuntimeActions({
      settingsState,
      runtimeDomain,
      oauthState,
    });

    expect(actions.setRoutingMode).toBe(settingsState.setRoutingMode);
    expect(actions.setRuntimeAction).toBe(runtimeDomain.setRuntimeAction);
    expect(actions.toggleRoutePause).toBe(runtimeDomain.toggleRoutePause);
    expect(actions.toggleAutoRestart).toBe(runtimeDomain.toggleAutoRestart);
    expect(actions.createListenerUrl).toBe(oauthState.createListenerUrl);
    expect(actions.toggleListener).toBe(oauthState.toggleListener);
    expect(actions.restartListener).toBe(oauthState.restartListener);
    expect(actions.initAuth0).toBe(oauthState.initAuth0);
    expect(actions.captureAuthResponse).toBe(oauthState.captureAuthResponse);
    expect(sortedKeys(actions)).toEqual([
      'captureAuthResponse',
      'createListenerUrl',
      'initAuth0',
      'restartListener',
      'setRoutingMode',
      'setRuntimeAction',
      'toggleAutoRestart',
      'toggleListener',
      'toggleRoutePause',
    ]);
  });

  test('buildDialogActions wires dialog handlers', () => {
    const uiState = {
      openBackendDialog: vi.fn(),
      closeBackendDialog: vi.fn(),
      openAuthDialog: vi.fn(),
      closeAuthDialog: vi.fn(),
      openSyncTopologyDialog: vi.fn(),
      closeSyncTopologyDialog: vi.fn(),
    };

    const actions = buildDialogActions({ uiState });

    expect(actions.openBackendDialog).toBe(uiState.openBackendDialog);
    expect(actions.closeBackendDialog).toBe(uiState.closeBackendDialog);
    expect(actions.openAuthDialog).toBe(uiState.openAuthDialog);
    expect(actions.closeAuthDialog).toBe(uiState.closeAuthDialog);
    expect(actions.openSyncTopologyDialog).toBe(uiState.openSyncTopologyDialog);
    expect(actions.closeSyncTopologyDialog).toBe(uiState.closeSyncTopologyDialog);
    expect(sortedKeys(actions)).toEqual([
      'closeAuthDialog',
      'closeBackendDialog',
      'closeSyncTopologyDialog',
      'openAuthDialog',
      'openBackendDialog',
      'openSyncTopologyDialog',
    ]);
  });

  test('buildSessionsAndLogsActions wires sessions and drawer handlers', () => {
    const uiState = {
      setSessionsKindFilter: vi.fn(),
      prevSessionsPage: vi.fn(),
      nextSessionsPage: vi.fn(),
      purgeStaleSessions: vi.fn(),
      openDrawer: vi.fn(),
      closeDrawer: vi.fn(),
    };

    const actions = buildSessionsAndLogsActions({ uiState });

    expect(actions.setSessionsKindFilter).toBe(uiState.setSessionsKindFilter);
    expect(actions.prevSessionsPage).toBe(uiState.prevSessionsPage);
    expect(actions.nextSessionsPage).toBe(uiState.nextSessionsPage);
    expect(actions.purgeStaleSessions).toBe(uiState.purgeStaleSessions);
    expect(actions.openDrawer).toBe(uiState.openDrawer);
    expect(actions.closeDrawer).toBe(uiState.closeDrawer);
    expect(sortedKeys(actions)).toEqual([
      'closeDrawer',
      'nextSessionsPage',
      'openDrawer',
      'prevSessionsPage',
      'purgeStaleSessions',
      'setSessionsKindFilter',
    ]);
  });
});
