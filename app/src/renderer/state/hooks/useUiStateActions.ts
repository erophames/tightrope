import { useEffect, type Dispatch, type SetStateAction } from 'react';
import type { Account, AccountState, AppRuntimeState, RouteRow } from '../../shared/types';
import { ensureSelectedRouteId, filteredRows } from '../logic';

export interface UseUiStateActionsOptions {
  state: AppRuntimeState;
  accounts: Account[];
  sessionsPageSize: number;
  setState: Dispatch<SetStateAction<AppRuntimeState>>;
}

export interface UseUiStateActionsResult {
  setSearchQuery: (searchQuery: string) => void;
  setSelectedAccountId: (accountId: string) => void;
  setSelectedRoute: (route: RouteRow) => void;
  setInspectorOpen: (open: boolean) => void;
  openBackendDialog: () => void;
  closeBackendDialog: () => void;
  openAuthDialog: () => void;
  closeAuthDialog: () => void;
  openSyncTopologyDialog: () => void;
  closeSyncTopologyDialog: () => void;
  setAccountSearchQuery: (query: string) => void;
  setAccountStatusFilter: (filter: '' | AccountState) => void;
  selectAccountDetail: (accountId: string) => void;
  setSessionsKindFilter: (kind: AppRuntimeState['sessionsKindFilter']) => void;
  prevSessionsPage: () => void;
  nextSessionsPage: () => void;
  purgeStaleSessions: () => void;
  openDrawer: (rowId: string) => void;
  closeDrawer: () => void;
}

export function useUiStateActions(options: UseUiStateActionsOptions): UseUiStateActionsResult {
  const { state, accounts, sessionsPageSize, setState } = options;

  const visibleRows = filteredRows(state.rows, accounts, state.selectedAccountId, state.searchQuery);
  const ensuredRouteId = ensureSelectedRouteId(visibleRows, state.selectedRouteId);

  useEffect(() => {
    if (ensuredRouteId !== state.selectedRouteId) {
      setState((previous) => ({ ...previous, selectedRouteId: ensuredRouteId }));
    }
  }, [ensuredRouteId, setState, state.selectedRouteId]);

  useEffect(() => {
    setState((previous) => {
      const hasSelectedRoutingAccount = previous.selectedAccountId
        ? accounts.some((account) => account.id === previous.selectedAccountId)
        : false;
      const nextSelectedAccountId = hasSelectedRoutingAccount ? previous.selectedAccountId : '';
      const hasSelectedDetail = previous.selectedAccountDetailId
        ? accounts.some((account) => account.id === previous.selectedAccountDetailId)
        : false;
      const nextSelectedDetailId = hasSelectedDetail ? previous.selectedAccountDetailId : null;
      if (nextSelectedAccountId === previous.selectedAccountId && nextSelectedDetailId === previous.selectedAccountDetailId) {
        return previous;
      }
      return {
        ...previous,
        selectedAccountId: nextSelectedAccountId,
        selectedAccountDetailId: nextSelectedDetailId,
      };
    });
  }, [accounts, setState]);

  useEffect(() => {
    const resolvedTheme =
      state.theme === 'auto' ? (window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light') : state.theme;
    document.documentElement.setAttribute('data-theme', resolvedTheme);
  }, [state.theme]);

  function setSearchQuery(searchQuery: string): void {
    setState((previous) => ({ ...previous, searchQuery }));
  }

  function setSelectedAccountId(accountId: string): void {
    setState((previous) => ({ ...previous, selectedAccountId: accountId }));
  }

  function setSelectedRoute(route: RouteRow): void {
    setState((previous) => ({
      ...previous,
      selectedRouteId: route.id,
      selectedAccountId: route.accountId,
      inspectorOpen: true,
    }));
  }

  function setInspectorOpen(open: boolean): void {
    setState((previous) => ({ ...previous, inspectorOpen: open }));
  }

  function openBackendDialog(): void {
    setState((previous) => ({ ...previous, backendDialogOpen: true }));
  }

  function closeBackendDialog(): void {
    setState((previous) => ({ ...previous, backendDialogOpen: false }));
  }

  function openAuthDialog(): void {
    setState((previous) => ({ ...previous, authDialogOpen: true }));
  }

  function closeAuthDialog(): void {
    setState((previous) => ({ ...previous, authDialogOpen: false }));
  }

  function openSyncTopologyDialog(): void {
    setState((previous) => ({ ...previous, syncTopologyDialogOpen: true }));
  }

  function closeSyncTopologyDialog(): void {
    setState((previous) => ({ ...previous, syncTopologyDialogOpen: false }));
  }

  function setAccountSearchQuery(query: string): void {
    setState((previous) => ({ ...previous, accountSearchQuery: query }));
  }

  function setAccountStatusFilter(filter: '' | AccountState): void {
    setState((previous) => ({ ...previous, accountStatusFilter: filter }));
  }

  function selectAccountDetail(accountId: string): void {
    setState((previous) => ({ ...previous, selectedAccountDetailId: accountId }));
  }

  function setSessionsKindFilter(kind: AppRuntimeState['sessionsKindFilter']): void {
    setState((previous) => ({
      ...previous,
      sessionsKindFilter: kind,
      sessionsOffset: 0,
    }));
  }

  function prevSessionsPage(): void {
    setState((previous) => ({
      ...previous,
      sessionsOffset: Math.max(0, previous.sessionsOffset - sessionsPageSize),
    }));
  }

  function nextSessionsPage(): void {
    setState((previous) => ({
      ...previous,
      sessionsOffset: previous.sessionsOffset + sessionsPageSize,
    }));
  }

  function purgeStaleSessions(): void {
    setState((previous) => ({
      ...previous,
      sessions: previous.sessions.filter((session) => !session.stale),
      sessionsOffset: 0,
    }));
  }

  function openDrawer(rowId: string): void {
    setState((previous) => ({ ...previous, drawerRowId: rowId }));
  }

  function closeDrawer(): void {
    setState((previous) => ({ ...previous, drawerRowId: null }));
  }

  return {
    setSearchQuery,
    setSelectedAccountId,
    setSelectedRoute,
    setInspectorOpen,
    openBackendDialog,
    closeBackendDialog,
    openAuthDialog,
    closeAuthDialog,
    openSyncTopologyDialog,
    closeSyncTopologyDialog,
    setAccountSearchQuery,
    setAccountStatusFilter,
    selectAccountDetail,
    setSessionsKindFilter,
    prevSessionsPage,
    nextSessionsPage,
    purgeStaleSessions,
    openDrawer,
    closeDrawer,
  };
}
