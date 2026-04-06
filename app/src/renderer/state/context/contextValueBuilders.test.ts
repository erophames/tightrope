import { describe, expect, test, vi } from 'vitest';
import { createInitialRuntimeState } from '../../test/fixtures/seed';
import type { TightropeModel } from './modelTypes';
import {
  buildAccountsContextValue,
  buildLogsContextValue,
  buildNavigationContextValue,
  buildRuntimeContextValue,
} from './contextValueBuilders';

describe('contextValueBuilders', () => {
  test('buildNavigationContextValue maps current page and handlers', () => {
    const state = createInitialRuntimeState();
    const setCurrentPage = vi.fn();
    const model = {
      state,
      settingsLeaveDialogOpen: true,
      settingsSaveInFlight: false,
      setCurrentPage,
      closeSettingsLeaveDialog: vi.fn(),
      discardSettingsAndNavigate: vi.fn(),
      saveSettingsAndNavigate: vi.fn(),
    } as unknown as TightropeModel;

    const value = buildNavigationContextValue(model);

    expect(value.currentPage).toBe('router');
    expect(value.settingsLeaveDialogOpen).toBe(true);
    expect(value.settingsSaveInFlight).toBe(false);
    expect(value.setCurrentPage).toBe(setCurrentPage);
  });

  test('buildRuntimeContextValue maps runtime/auth dialog state and actions', () => {
    const state = createInitialRuntimeState();
    state.backendDialogOpen = true;
    state.authDialogOpen = true;
    const toggleListener = vi.fn();
    const model = {
      state,
      setRuntimeAction: vi.fn(),
      toggleRoutePause: vi.fn(),
      toggleAutoRestart: vi.fn(),
      openBackendDialog: vi.fn(),
      closeBackendDialog: vi.fn(),
      openAuthDialog: vi.fn(),
      closeAuthDialog: vi.fn(),
      createListenerUrl: vi.fn(),
      toggleListener,
      restartListener: vi.fn(),
      initAuth0: vi.fn(),
      captureAuthResponse: vi.fn(),
    } as unknown as TightropeModel;

    const value = buildRuntimeContextValue(model);

    expect(value.backendDialogOpen).toBe(true);
    expect(value.authDialogOpen).toBe(true);
    expect(value.runtimeState).toEqual(state.runtimeState);
    expect(value.authState).toEqual(state.authState);
    expect(value.toggleListener).toBe(toggleListener);
  });

  test('buildAccountsContextValue and buildLogsContextValue map derived state + handlers', () => {
    const state = createInitialRuntimeState();
    state.accountSearchQuery = 'alice';
    state.accountStatusFilter = 'active';
    state.addAccountOpen = true;
    state.selectedFileName = 'auth.json';
    state.drawerRowId = state.rows[0].id;
    const openDrawer = vi.fn();
    const accounts = [
      {
        id: 'acc-1',
        name: 'alice@test.local',
        plan: 'free',
        health: 'healthy',
        state: 'active',
        inflight: 0,
        load: 0,
        latency: 0,
        errorEwma: 0,
        cooldown: false,
        capability: true,
        costNorm: 0,
        routed24h: 0,
        stickyHit: 0,
        quotaPrimary: 0,
        quotaSecondary: 0,
        failovers: 0,
        note: 'openai',
        telemetryBacked: false,
      },
    ];
    const model = {
      state,
      accounts,
      filteredAccounts: accounts,
      selectedAccountDetail: accounts[0],
      selectedAccountUsage24h: { requests: 1, tokens: 2, costUsd: 3, failovers: 4 },
      trafficClockMs: 1000,
      trafficActiveWindowMs: 3000,
      isRefreshingSelectedAccountTelemetry: false,
      isRefreshingSelectedAccountToken: false,
      stableSparklinePercents: vi.fn(() => [1, 2, 3]),
      formatNumber: vi.fn((value: number) => String(value)),
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
      setAccountSearchQuery: vi.fn(),
      setAccountStatusFilter: vi.fn(),
      selectAccountDetail: vi.fn(),
      toggleAccountPin: vi.fn(),
      refreshSelectedAccountTelemetry: vi.fn(),
      refreshSelectedAccountToken: vi.fn(),
      pauseSelectedAccount: vi.fn(),
      reactivateSelectedAccount: vi.fn(),
      deleteSelectedAccount: vi.fn(),
      openDrawer,
      closeDrawer: vi.fn(),
    } as unknown as TightropeModel;

    const accountsValue = buildAccountsContextValue(model);
    const logsValue = buildLogsContextValue(model, state.rows[0]);

    expect(accountsValue.accountSearchQuery).toBe('alice');
    expect(accountsValue.accountStatusFilter).toBe('active');
    expect(accountsValue.selectedFileName).toBe('auth.json');
    expect(accountsValue.addAccountOpen).toBe(true);
    expect(accountsValue.accounts).toHaveLength(1);
    expect(logsValue.rows).toEqual(state.rows);
    expect(logsValue.drawerRow).toEqual(state.rows[0]);
    expect(logsValue.openDrawer).toBe(openDrawer);
  });

  test('buildNavigationContextValue and buildLogsContextValue preserve empty/closed state', () => {
    const state = createInitialRuntimeState();
    const model = {
      state,
      settingsLeaveDialogOpen: false,
      settingsSaveInFlight: true,
      setCurrentPage: vi.fn(),
      closeSettingsLeaveDialog: vi.fn(),
      discardSettingsAndNavigate: vi.fn(),
      saveSettingsAndNavigate: vi.fn(),
      openDrawer: vi.fn(),
      closeDrawer: vi.fn(),
    } as unknown as TightropeModel;

    const navigationValue = buildNavigationContextValue(model);
    const logsValue = buildLogsContextValue(model, null);

    expect(navigationValue.settingsLeaveDialogOpen).toBe(false);
    expect(navigationValue.settingsSaveInFlight).toBe(true);
    expect(logsValue.drawerRow).toBeNull();
  });
});
