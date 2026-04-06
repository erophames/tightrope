import type { Account, AppRuntimeState } from '../../shared/types';
import { deriveAccountUsage24h } from './useAccountUsage';

export interface UseAccountsDerivedViewOptions {
  state: AppRuntimeState;
  accounts: Account[];
  isRefreshingAccountTelemetry: (accountId: string | null | undefined) => boolean;
}

interface UseAccountsDerivedViewResult {
  filteredAccounts: Account[];
  selectedAccountDetail: Account | null;
  selectedAccountUsage24h: {
    requests: number;
    tokens: number;
    costUsd: number;
    failovers: number;
  };
  isRefreshingSelectedAccountTelemetry: boolean;
}

export function useAccountsDerivedView(options: UseAccountsDerivedViewOptions): UseAccountsDerivedViewResult {
  const { state, accounts, isRefreshingAccountTelemetry } = options;

  const accountFilterQuery = state.accountSearchQuery.trim().toLowerCase();
  const filteredAccounts = accounts.filter((account) => {
    if (state.accountStatusFilter && account.state !== state.accountStatusFilter) return false;
    if (!accountFilterQuery) return true;
    return account.name.toLowerCase().includes(accountFilterQuery) || account.id.toLowerCase().includes(accountFilterQuery);
  });

  const selectedAccountDetail = accounts.find((account) => account.id === state.selectedAccountDetailId) ?? null;
  const selectedAccountUsage24h = deriveAccountUsage24h(state.rows, selectedAccountDetail?.id ?? null);
  const isRefreshingSelectedAccountTelemetry = isRefreshingAccountTelemetry(selectedAccountDetail?.id);

  return {
    filteredAccounts,
    selectedAccountDetail,
    selectedAccountUsage24h,
    isRefreshingSelectedAccountTelemetry,
  };
}
