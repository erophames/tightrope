import { useMemo, type ReactNode } from 'react';
import { AccountsContext } from './AccountsContext';
import { LogsContext } from './LogsContext';
import { NavigationContext } from './NavigationContext';
import { RouterDerivedContext } from './RouterDerivedContext';
import { RuntimeContext } from './RuntimeContext';
import { SessionsContext } from './SessionsContext';
import { SettingsContext } from './SettingsContext';
import { TightropeServiceProvider, useTightropeService } from './TightropeServiceContext';
import {
  buildAccountsContextValue,
  buildLogsContextValue,
  buildNavigationContextValue,
  buildRuntimeContextValue,
} from './contextValueBuilders';
import {
  useRouterDerivedContextValue,
  useSessionsContextValue,
  useSettingsContextValue,
} from './useDerivedContextValues';
import type { TightropeService } from '../../services/tightrope';
import type { TightropeModel } from './modelTypes';
import { useTightropeModel } from '../hooks/useTightropeModel';

interface AppStateProvidersProps {
  children: ReactNode;
  service?: TightropeService;
}

function AppStateProvidersInner({ children }: { children: ReactNode }) {
  const service = useTightropeService();
  const model: TightropeModel = useTightropeModel(service);

  const navigationValue = useMemo(
    () => buildNavigationContextValue(model),
    [
      model.state.currentPage,
      model.settingsLeaveDialogOpen,
      model.settingsSaveInFlight,
      model.setCurrentPage,
      model.closeSettingsLeaveDialog,
      model.discardSettingsAndNavigate,
      model.saveSettingsAndNavigate,
    ],
  );
  const runtimeValue = useMemo(
    () => buildRuntimeContextValue(model),
    [
      model.state.runtimeState,
      model.state.authState,
      model.state.backendDialogOpen,
      model.state.authDialogOpen,
      model.setRuntimeAction,
      model.toggleRoutePause,
      model.toggleAutoRestart,
      model.openBackendDialog,
      model.closeBackendDialog,
      model.openAuthDialog,
      model.closeAuthDialog,
      model.createListenerUrl,
      model.toggleListener,
      model.restartListener,
      model.initAuth0,
      model.captureAuthResponse,
    ],
  );
  const accountsValue = useMemo(
    () => buildAccountsContextValue(model),
    [
      model.accounts,
      model.filteredAccounts,
      model.selectedAccountDetail,
      model.selectedAccountUsage24h,
      model.trafficClockMs,
      model.trafficActiveWindowMs,
      model.state.accountSearchQuery,
      model.state.accountStatusFilter,
      model.isRefreshingSelectedAccountTelemetry,
      model.stableSparklinePercents,
      model.formatNumber,
      model.state.addAccountOpen,
      model.state.addAccountStep,
      model.state.selectedFileName,
      model.state.manualCallback,
      model.state.browserAuthUrl,
      model.state.deviceVerifyUrl,
      model.state.deviceUserCode,
      model.state.deviceCountdownSeconds,
      model.state.copyAuthLabel,
      model.state.copyDeviceLabel,
      model.state.successEmail,
      model.state.successPlan,
      model.state.addAccountError,
      model.openAddAccountDialog,
      model.closeAddAccountDialog,
      model.setAddAccountStep,
      model.selectImportFile,
      model.submitImport,
      model.simulateBrowserAuth,
      model.submitManualCallback,
      model.setManualCallback,
      model.copyBrowserAuthUrl,
      model.copyDeviceVerificationUrl,
      model.startDeviceFlow,
      model.cancelDeviceFlow,
      model.setAccountSearchQuery,
      model.setAccountStatusFilter,
      model.selectAccountDetail,
      model.toggleAccountPin,
      model.refreshSelectedAccountTelemetry,
      model.pauseSelectedAccount,
      model.reactivateSelectedAccount,
      model.deleteSelectedAccount,
    ],
  );

  const routerDerivedValue = useRouterDerivedContextValue(model);
  const sessionsValue = useSessionsContextValue(model);
  const settingsValue = useSettingsContextValue(model);

  const drawerRow = useMemo(
    () => model.state.rows.find((row) => row.id === model.state.drawerRowId) ?? null,
    [model.state.drawerRowId, model.state.rows],
  );
  const logsValue = useMemo(
    () => buildLogsContextValue(model, drawerRow),
    [drawerRow, model.state.rows, model.openDrawer, model.closeDrawer],
  );

  return (
    <NavigationContext.Provider value={navigationValue}>
      <RuntimeContext.Provider value={runtimeValue}>
        <AccountsContext.Provider value={accountsValue}>
          <RouterDerivedContext.Provider value={routerDerivedValue}>
            <SessionsContext.Provider value={sessionsValue}>
              <LogsContext.Provider value={logsValue}>
                <SettingsContext.Provider value={settingsValue}>{children}</SettingsContext.Provider>
              </LogsContext.Provider>
            </SessionsContext.Provider>
          </RouterDerivedContext.Provider>
        </AccountsContext.Provider>
      </RuntimeContext.Provider>
    </NavigationContext.Provider>
  );
}

export function AppStateProviders({ children, service }: AppStateProvidersProps) {
  return (
    <TightropeServiceProvider service={service}>
      <AppStateProvidersInner>{children}</AppStateProvidersInner>
    </TightropeServiceProvider>
  );
}
