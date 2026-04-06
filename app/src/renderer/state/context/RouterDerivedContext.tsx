import { createContext, useContext } from 'react';
import type { Account, RouteMetrics, RouteRow } from '../../shared/types';

export interface RouterDerivedContextValue {
  metrics: Map<string, RouteMetrics>;
  visibleRows: RouteRow[];
  searchQuery: string;
  selectedAccountId: string;
  selectedRouteId: string;
  inspectorOpen: boolean;
  routedAccountId: string | null;
  selectedRoute: RouteRow;
  selectedRouteAccount: Account;
  selectedMetric: RouteMetrics | undefined;
  kpis: {
    rpm: number;
    p95: number;
    failover: number;
    sticky: number;
  };
  routerState: 'running' | 'paused' | 'degraded' | 'stopped';
  modeLabel: string;
  setSearchQuery: (searchQuery: string) => void;
  setSelectedAccountId: (accountId: string) => void;
  setSelectedRoute: (route: RouteRow) => void;
  setInspectorOpen: (open: boolean) => void;
}

export const RouterDerivedContext = createContext<RouterDerivedContextValue | null>(null);

export function useRouterDerivedContext(): RouterDerivedContextValue {
  const context = useContext(RouterDerivedContext);
  if (!context) {
    throw new Error('useRouterDerivedContext must be used within AppStateProviders');
  }
  return context;
}
