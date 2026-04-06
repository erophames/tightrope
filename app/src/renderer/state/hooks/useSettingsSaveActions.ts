import { useCallback } from 'react';
import type { DashboardSettings } from '../../shared/types';
import { syncSettingsFingerprint } from './useSettings';

interface SaveDashboardSettingsResult {
  saved: boolean;
  previousApplied: DashboardSettings | null;
  updated: DashboardSettings | null;
}

export interface UseSettingsSaveActionsOptions {
  settingsState: {
    discardDashboardSettingsChanges: () => void;
    saveDashboardSettings: () => Promise<SaveDashboardSettingsResult>;
  };
  clusterSyncEnabled: boolean;
  reconfigureSyncCluster: (settings: DashboardSettings) => Promise<void>;
}

interface UseSettingsSaveActionsResult {
  discardDashboardSettingsChanges: () => void;
  saveDashboardSettings: () => Promise<boolean>;
  saveSettingsChanges: () => void;
}

export function useSettingsSaveActions(options: UseSettingsSaveActionsOptions): UseSettingsSaveActionsResult {
  const { settingsState, clusterSyncEnabled, reconfigureSyncCluster } = options;
  const { discardDashboardSettingsChanges, saveDashboardSettings: saveSettingsDraft } = settingsState;

  const saveDashboardSettings = useCallback(async (): Promise<boolean> => {
    const result = await saveSettingsDraft();
    if (!result.saved || !result.previousApplied || !result.updated) {
      return false;
    }
    if (
      clusterSyncEnabled &&
      syncSettingsFingerprint(result.previousApplied) !== syncSettingsFingerprint(result.updated)
    ) {
      await reconfigureSyncCluster(result.updated);
    }
    return true;
  }, [clusterSyncEnabled, reconfigureSyncCluster, saveSettingsDraft]);

  const saveSettingsChanges = useCallback((): void => {
    void saveDashboardSettings();
  }, [saveDashboardSettings]);

  return {
    discardDashboardSettingsChanges,
    saveDashboardSettings,
    saveSettingsChanges,
  };
}
