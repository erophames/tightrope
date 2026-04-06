import { useCallback, type Dispatch, type SetStateAction } from 'react';
import type { AppRuntimeState } from '../../shared/types';

export interface UseSelectedAccountActionsOptions {
  selectedAccountDetailId: string | null;
  setState: Dispatch<SetStateAction<AppRuntimeState>>;
  toggleAccountPin: (accountId: string, nextPinned: boolean) => Promise<void>;
  pauseAccount: (accountId: string) => Promise<void>;
  reactivateAccount: (accountId: string) => Promise<void>;
  deleteAccount: (accountId: string) => Promise<boolean>;
  refreshAccountTelemetry: (accountId: string) => Promise<void>;
  refreshAccountToken: (accountId: string) => Promise<void>;
  refreshAllAccountsTelemetry: () => Promise<void>;
}

interface UseSelectedAccountActionsResult {
  toggleAccountPin: (accountId: string, nextPinned: boolean) => Promise<void>;
  pauseSelectedAccount: () => Promise<void>;
  reactivateSelectedAccount: () => Promise<void>;
  deleteSelectedAccount: () => Promise<void>;
  refreshSelectedAccountTelemetry: () => Promise<void>;
  refreshSelectedAccountToken: () => Promise<void>;
  refreshAllAccountsTelemetry: () => Promise<void>;
}

export function useSelectedAccountActions(options: UseSelectedAccountActionsOptions): UseSelectedAccountActionsResult {
  const {
    selectedAccountDetailId,
    setState,
    toggleAccountPin,
    pauseAccount,
    reactivateAccount,
    deleteAccount,
    refreshAccountTelemetry,
    refreshAccountToken,
    refreshAllAccountsTelemetry,
  } = options;

  const pauseSelectedAccount = useCallback(async (): Promise<void> => {
    if (!selectedAccountDetailId) {
      return;
    }
    await pauseAccount(selectedAccountDetailId);
  }, [pauseAccount, selectedAccountDetailId]);

  const reactivateSelectedAccount = useCallback(async (): Promise<void> => {
    if (!selectedAccountDetailId) {
      return;
    }
    await reactivateAccount(selectedAccountDetailId);
  }, [reactivateAccount, selectedAccountDetailId]);

  const deleteSelectedAccount = useCallback(async (): Promise<void> => {
    if (!selectedAccountDetailId) {
      return;
    }
    const deleted = await deleteAccount(selectedAccountDetailId);
    if (!deleted) {
      return;
    }
    setState((previous) => ({
      ...previous,
      selectedAccountDetailId: null,
    }));
  }, [deleteAccount, selectedAccountDetailId, setState]);

  const refreshSelectedAccountTelemetry = useCallback(async (): Promise<void> => {
    if (!selectedAccountDetailId) {
      return;
    }
    await refreshAccountTelemetry(selectedAccountDetailId);
  }, [refreshAccountTelemetry, selectedAccountDetailId]);

  const refreshSelectedAccountToken = useCallback(async (): Promise<void> => {
    if (!selectedAccountDetailId) {
      return;
    }
    await refreshAccountToken(selectedAccountDetailId);
  }, [refreshAccountToken, selectedAccountDetailId]);

  return {
    toggleAccountPin,
    pauseSelectedAccount,
    reactivateSelectedAccount,
    deleteSelectedAccount,
    refreshSelectedAccountTelemetry,
    refreshSelectedAccountToken,
    refreshAllAccountsTelemetry,
  };
}
