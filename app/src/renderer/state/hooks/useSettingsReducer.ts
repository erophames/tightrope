import type { DashboardSettings } from '../../shared/types';

export interface SettingsReducerState {
  dashboardSettings: DashboardSettings;
  appliedDashboardSettings: DashboardSettings;
  settingsSaveInFlight: boolean;
}

export type SettingsReducerAction =
  | { type: 'apply_draft'; nextSettings: DashboardSettings }
  | { type: 'apply_applied'; nextSettings: DashboardSettings }
  | { type: 'set_saving'; value: boolean };

export function createInitialSettingsReducerState(seed: DashboardSettings): SettingsReducerState {
  return {
    dashboardSettings: { ...seed },
    appliedDashboardSettings: { ...seed },
    settingsSaveInFlight: false,
  };
}

export function settingsReducer(state: SettingsReducerState, action: SettingsReducerAction): SettingsReducerState {
  switch (action.type) {
    case 'apply_draft':
      return {
        ...state,
        dashboardSettings: action.nextSettings,
      };
    case 'apply_applied':
      return {
        ...state,
        appliedDashboardSettings: action.nextSettings,
      };
    case 'set_saving':
      return {
        ...state,
        settingsSaveInFlight: action.value,
      };
    default:
      return state;
  }
}
