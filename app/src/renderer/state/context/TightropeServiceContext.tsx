import { createContext, useContext, useMemo, type ReactNode } from 'react';
import { createTightropeService, type TightropeService } from '../../services/tightrope';

const TightropeServiceContext = createContext<TightropeService | null>(null);

interface TightropeServiceProviderProps {
  children: ReactNode;
  service?: TightropeService;
}

export function TightropeServiceProvider({ children, service }: TightropeServiceProviderProps) {
  const resolvedService = useMemo<TightropeService>(() => service ?? createTightropeService(), [service]);
  return <TightropeServiceContext.Provider value={resolvedService}>{children}</TightropeServiceContext.Provider>;
}

export function useTightropeService(): TightropeService {
  const value = useContext(TightropeServiceContext);
  if (!value) {
    throw new Error('TightropeServiceProvider missing');
  }
  return value;
}
