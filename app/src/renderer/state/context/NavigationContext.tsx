import { createContext, useContext } from 'react';
import type { AppPage } from '../../shared/types';

export interface NavigationContextValue {
  currentPage: AppPage;
  settingsLeaveDialogOpen: boolean;
  settingsSaveInFlight: boolean;
  setCurrentPage: (page: AppPage) => void;
  closeSettingsLeaveDialog: () => void;
  discardSettingsAndNavigate: () => void;
  saveSettingsAndNavigate: () => Promise<void>;
}

export const NavigationContext = createContext<NavigationContextValue | null>(null);

export function useNavigationContext(): NavigationContextValue {
  const context = useContext(NavigationContext);
  if (!context) {
    throw new Error('useNavigationContext must be used within AppStateProviders');
  }
  return context;
}
