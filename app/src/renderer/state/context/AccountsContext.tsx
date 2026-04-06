import { createContext, useContext } from 'react';
import type { Account, AccountState, AddAccountStep } from '../../shared/types';

export interface AccountsContextValue {
  accounts: Account[];
  filteredAccounts: Account[];
  selectedAccountDetail: Account | null;
  selectedAccountUsage24h: {
    requests: number;
    tokens: number;
    costUsd: number;
    failovers: number;
  };
  trafficClockMs: number;
  trafficActiveWindowMs: number;
  accountSearchQuery: string;
  accountStatusFilter: '' | AccountState;
  isRefreshingSelectedAccountTelemetry: boolean;
  isRefreshingSelectedAccountToken: boolean;
  isRefreshingAllAccountTelemetry: boolean;
  stableSparklinePercents: (key: string, currentPct: number) => number[];
  formatNumber: (value: number, maximumFractionDigits?: number) => string;
  addAccountOpen: boolean;
  addAccountStep: AddAccountStep;
  selectedFileName: string;
  manualCallback: string;
  browserAuthUrl: string;
  deviceVerifyUrl: string;
  deviceUserCode: string;
  deviceCountdownSeconds: number;
  copyAuthLabel: 'Copy' | 'Copied';
  copyDeviceLabel: 'Copy' | 'Copied';
  successEmail: string;
  successPlan: string;
  addAccountError: string;
  openAddAccountDialog: () => void;
  closeAddAccountDialog: () => void;
  setAddAccountStep: (step: AddAccountStep) => void;
  selectImportFile: (file: File) => void;
  submitImport: () => void;
  simulateBrowserAuth: () => void;
  submitManualCallback: () => void;
  setManualCallback: (value: string) => void;
  copyBrowserAuthUrl: () => Promise<void>;
  copyDeviceVerificationUrl: () => Promise<void>;
  startDeviceFlow: () => void;
  cancelDeviceFlow: () => void;
  setAccountSearchQuery: (query: string) => void;
  setAccountStatusFilter: (filter: '' | AccountState) => void;
  selectAccountDetail: (accountId: string) => void;
  toggleAccountPin: (accountId: string, nextPinned: boolean) => Promise<void>;
  refreshSelectedAccountTelemetry: () => Promise<void>;
  refreshSelectedAccountToken: () => Promise<void>;
  refreshAllAccountsTelemetry: () => Promise<void>;
  pauseSelectedAccount: () => Promise<void>;
  reactivateSelectedAccount: () => Promise<void>;
  deleteSelectedAccount: () => Promise<void>;
}

export const AccountsContext = createContext<AccountsContextValue | null>(null);

export function useAccountsContext(): AccountsContextValue {
  const context = useContext(AccountsContext);
  if (!context) {
    throw new Error('useAccountsContext must be used within AppStateProviders');
  }
  return context;
}
