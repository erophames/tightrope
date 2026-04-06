import { useCallback, useState, type Dispatch, type SetStateAction } from 'react';
import type { AppPage, AppRuntimeState } from '../../shared/types';

export interface UseNavigationOptions {
  currentPage: AppPage;
  hasUnsavedSettingsChanges: boolean;
  settingsSaveInFlight: boolean;
  setState: Dispatch<SetStateAction<AppRuntimeState>>;
  discardDashboardSettingsChanges: () => void;
  saveDashboardSettings: () => Promise<boolean>;
}

interface UseNavigationResult {
  settingsLeaveTargetPage: AppPage | null;
  settingsLeaveDialogOpen: boolean;
  setCurrentPage: (page: AppPage) => void;
  closeSettingsLeaveDialog: () => void;
  discardSettingsAndNavigate: () => void;
  saveSettingsAndNavigate: () => Promise<void>;
}

export function useNavigation(options: UseNavigationOptions): UseNavigationResult {
  const {
    currentPage,
    hasUnsavedSettingsChanges,
    settingsSaveInFlight,
    setState,
    discardDashboardSettingsChanges,
    saveDashboardSettings,
  } = options;

  const [settingsLeaveTargetPage, setSettingsLeaveTargetPage] = useState<AppPage | null>(null);

  const setCurrentPage = useCallback((page: AppPage): void => {
    if (page === currentPage) {
      return;
    }
    if (currentPage === 'settings' && page !== 'settings' && hasUnsavedSettingsChanges) {
      setSettingsLeaveTargetPage(page);
      return;
    }
    setState((previous) => ({ ...previous, currentPage: page }));
  }, [currentPage, hasUnsavedSettingsChanges, setState]);

  const closeSettingsLeaveDialog = useCallback((): void => {
    if (settingsSaveInFlight) {
      return;
    }
    setSettingsLeaveTargetPage(null);
  }, [settingsSaveInFlight]);

  const discardSettingsAndNavigate = useCallback((): void => {
    if (settingsLeaveTargetPage === null) {
      return;
    }

    const targetPage = settingsLeaveTargetPage;
    discardDashboardSettingsChanges();
    setSettingsLeaveTargetPage(null);
    setState((previous) => ({ ...previous, currentPage: targetPage }));
  }, [discardDashboardSettingsChanges, setState, settingsLeaveTargetPage]);

  const saveSettingsAndNavigate = useCallback(async (): Promise<void> => {
    if (settingsLeaveTargetPage === null) {
      return;
    }

    const targetPage = settingsLeaveTargetPage;
    const saved = await saveDashboardSettings();
    if (!saved) {
      return;
    }
    setSettingsLeaveTargetPage(null);
    setState((previous) => ({ ...previous, currentPage: targetPage }));
  }, [saveDashboardSettings, setState, settingsLeaveTargetPage]);

  return {
    settingsLeaveTargetPage,
    settingsLeaveDialogOpen: settingsLeaveTargetPage !== null,
    setCurrentPage,
    closeSettingsLeaveDialog,
    discardSettingsAndNavigate,
    saveSettingsAndNavigate,
  };
}
