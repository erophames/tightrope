import { describe, expect, test, vi } from 'vitest';
import { createInitialRuntimeState } from '../../test/fixtures/seed';
import {
  buildTightropeModel,
  type TightropeModelAccountActionsBundle,
  type TightropeModelDialogActions,
  type TightropeModelNavigationActions,
  type TightropeModelRouterActions,
  type TightropeModelRuntimeActions,
  type TightropeModelSessionsAndLogsActions,
  type TightropeModelSettingsControls,
  type TightropeModelStateData,
} from './tightropeModelComposition';

describe('tightropeModelComposition', () => {
  test('buildTightropeModel merges bundle slices into a single contract object', () => {
    const setCurrentPage = vi.fn();
    const setSearchQuery = vi.fn();
    const setRuntimeAction = vi.fn();
    const setTheme = vi.fn();
    const openBackendDialog = vi.fn();
    const openAddAccountDialog = vi.fn();
    const openDrawer = vi.fn();

    const stateData = {
      state: createInitialRuntimeState(),
      accounts: [],
    } as unknown as TightropeModelStateData;

    const model = buildTightropeModel({
      stateData,
      navigationActions: { setCurrentPage } as unknown as TightropeModelNavigationActions,
      routerActions: { setSearchQuery } as unknown as TightropeModelRouterActions,
      runtimeActions: { setRuntimeAction } as unknown as TightropeModelRuntimeActions,
      settingsControls: { setTheme } as unknown as TightropeModelSettingsControls,
      dialogActions: { openBackendDialog } as unknown as TightropeModelDialogActions,
      accountActionsBundle: { openAddAccountDialog } as unknown as TightropeModelAccountActionsBundle,
      sessionsAndLogsActions: { openDrawer } as unknown as TightropeModelSessionsAndLogsActions,
    });

    expect(model.state).toBe(stateData.state);
    expect(model.accounts).toBe(stateData.accounts);
    expect(model.setCurrentPage).toBe(setCurrentPage);
    expect(model.setSearchQuery).toBe(setSearchQuery);
    expect(model.setRuntimeAction).toBe(setRuntimeAction);
    expect(model.setTheme).toBe(setTheme);
    expect(model.openBackendDialog).toBe(openBackendDialog);
    expect(model.openAddAccountDialog).toBe(openAddAccountDialog);
    expect(model.openDrawer).toBe(openDrawer);
  });
});
