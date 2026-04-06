import { describe, expect, test } from 'vitest';
import { defaultDashboardSettings } from './useSettings';
import { createInitialSettingsReducerState, settingsReducer } from './useSettingsReducer';

describe('useSettingsReducer', () => {
  test('createInitialSettingsReducerState seeds draft/applied/saving values', () => {
    const state = createInitialSettingsReducerState(defaultDashboardSettings);

    expect(state.dashboardSettings.theme).toBe('auto');
    expect(state.appliedDashboardSettings.theme).toBe('auto');
    expect(state.settingsSaveInFlight).toBe(false);
  });

  test('apply_draft updates dashboardSettings only', () => {
    const initial = createInitialSettingsReducerState(defaultDashboardSettings);
    const nextDraft = { ...defaultDashboardSettings, theme: 'dark' as const };

    const next = settingsReducer(initial, { type: 'apply_draft', nextSettings: nextDraft });

    expect(next.dashboardSettings.theme).toBe('dark');
    expect(next.appliedDashboardSettings.theme).toBe('auto');
  });

  test('apply_applied updates appliedDashboardSettings only', () => {
    const initial = createInitialSettingsReducerState(defaultDashboardSettings);
    const applied = { ...defaultDashboardSettings, theme: 'light' as const };

    const next = settingsReducer(initial, { type: 'apply_applied', nextSettings: applied });

    expect(next.dashboardSettings.theme).toBe('auto');
    expect(next.appliedDashboardSettings.theme).toBe('light');
  });

  test('set_saving toggles settingsSaveInFlight', () => {
    const initial = createInitialSettingsReducerState(defaultDashboardSettings);

    const saving = settingsReducer(initial, { type: 'set_saving', value: true });
    const idle = settingsReducer(saving, { type: 'set_saving', value: false });

    expect(saving.settingsSaveInFlight).toBe(true);
    expect(idle.settingsSaveInFlight).toBe(false);
  });
});
