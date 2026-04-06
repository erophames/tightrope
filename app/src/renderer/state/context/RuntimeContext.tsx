import { createContext, useContext } from 'react';
import type { AppRuntimeState } from '../../shared/types';

export interface RuntimeContextValue {
  runtimeState: AppRuntimeState['runtimeState'];
  authState: AppRuntimeState['authState'];
  backendDialogOpen: boolean;
  authDialogOpen: boolean;
  setRuntimeAction: (action: 'start' | 'restart' | 'stop') => void;
  toggleRoutePause: () => void;
  toggleAutoRestart: () => void;
  openBackendDialog: () => void;
  closeBackendDialog: () => void;
  openAuthDialog: () => void;
  closeAuthDialog: () => void;
  createListenerUrl: () => Promise<string | null>;
  toggleListener: () => Promise<void>;
  restartListener: () => Promise<void>;
  initAuth0: () => Promise<void>;
  captureAuthResponse: () => Promise<void>;
}

export const RuntimeContext = createContext<RuntimeContextValue | null>(null);

export function useRuntimeContext(): RuntimeContextValue {
  const context = useContext(RuntimeContext);
  if (!context) {
    throw new Error('useRuntimeContext must be used within AppStateProviders');
  }
  return context;
}
