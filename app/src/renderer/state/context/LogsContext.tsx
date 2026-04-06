import { createContext, useContext } from 'react';
import type { RouteRow } from '../../shared/types';

export interface LogsContextValue {
  rows: RouteRow[];
  drawerRow: RouteRow | null;
  openDrawer: (rowId: string) => void;
  closeDrawer: () => void;
}

export const LogsContext = createContext<LogsContextValue | null>(null);

export function useLogsContext(): LogsContextValue {
  const context = useContext(LogsContext);
  if (!context) {
    throw new Error('useLogsContext must be used within AppStateProviders');
  }
  return context;
}
