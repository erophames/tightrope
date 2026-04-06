import type { AccountsContextValue } from './AccountsContext';
import type { LogsContextValue } from './LogsContext';
import type { NavigationContextValue } from './NavigationContext';
import type { RuntimeContextValue } from './RuntimeContext';
import type { TightropeModel } from './modelTypes';

export function buildNavigationContextValue(model: TightropeModel): NavigationContextValue {
  return {
    currentPage: model.state.currentPage,
    settingsLeaveDialogOpen: model.settingsLeaveDialogOpen,
    settingsSaveInFlight: model.settingsSaveInFlight,
    setCurrentPage: model.setCurrentPage,
    closeSettingsLeaveDialog: model.closeSettingsLeaveDialog,
    discardSettingsAndNavigate: model.discardSettingsAndNavigate,
    saveSettingsAndNavigate: model.saveSettingsAndNavigate,
  };
}

export function buildRuntimeContextValue(model: TightropeModel): RuntimeContextValue {
  return {
    runtimeState: model.state.runtimeState,
    authState: model.state.authState,
    backendDialogOpen: model.state.backendDialogOpen,
    authDialogOpen: model.state.authDialogOpen,
    setRuntimeAction: model.setRuntimeAction,
    toggleRoutePause: model.toggleRoutePause,
    toggleAutoRestart: model.toggleAutoRestart,
    openBackendDialog: model.openBackendDialog,
    closeBackendDialog: model.closeBackendDialog,
    openAuthDialog: model.openAuthDialog,
    closeAuthDialog: model.closeAuthDialog,
    createListenerUrl: model.createListenerUrl,
    toggleListener: model.toggleListener,
    restartListener: model.restartListener,
    initAuth0: model.initAuth0,
    captureAuthResponse: model.captureAuthResponse,
  };
}

export function buildAccountsContextValue(model: TightropeModel): AccountsContextValue {
  return {
    accounts: model.accounts,
    filteredAccounts: model.filteredAccounts,
    selectedAccountDetail: model.selectedAccountDetail,
    selectedAccountUsage24h: model.selectedAccountUsage24h,
    trafficClockMs: model.trafficClockMs,
    trafficActiveWindowMs: model.trafficActiveWindowMs,
    accountSearchQuery: model.state.accountSearchQuery,
    accountStatusFilter: model.state.accountStatusFilter,
    isRefreshingSelectedAccountTelemetry: model.isRefreshingSelectedAccountTelemetry,
    isRefreshingSelectedAccountToken: model.isRefreshingSelectedAccountToken,
    isRefreshingAllAccountTelemetry: model.isRefreshingAllAccountTelemetry,
    stableSparklinePercents: model.stableSparklinePercents,
    formatNumber: model.formatNumber,
    addAccountOpen: model.state.addAccountOpen,
    addAccountStep: model.state.addAccountStep,
    selectedFileName: model.state.selectedFileName,
    manualCallback: model.state.manualCallback,
    browserAuthUrl: model.state.browserAuthUrl,
    deviceVerifyUrl: model.state.deviceVerifyUrl,
    deviceUserCode: model.state.deviceUserCode,
    deviceCountdownSeconds: model.state.deviceCountdownSeconds,
    copyAuthLabel: model.state.copyAuthLabel,
    copyDeviceLabel: model.state.copyDeviceLabel,
    successEmail: model.state.successEmail,
    successPlan: model.state.successPlan,
    addAccountError: model.state.addAccountError,
    openAddAccountDialog: model.openAddAccountDialog,
    closeAddAccountDialog: model.closeAddAccountDialog,
    setAddAccountStep: model.setAddAccountStep,
    selectImportFile: model.selectImportFile,
    submitImport: model.submitImport,
    simulateBrowserAuth: model.simulateBrowserAuth,
    submitManualCallback: model.submitManualCallback,
    setManualCallback: model.setManualCallback,
    copyBrowserAuthUrl: model.copyBrowserAuthUrl,
    copyDeviceVerificationUrl: model.copyDeviceVerificationUrl,
    startDeviceFlow: model.startDeviceFlow,
    cancelDeviceFlow: model.cancelDeviceFlow,
    setAccountSearchQuery: model.setAccountSearchQuery,
    setAccountStatusFilter: model.setAccountStatusFilter,
    selectAccountDetail: model.selectAccountDetail,
    toggleAccountPin: model.toggleAccountPin,
    refreshSelectedAccountTelemetry: model.refreshSelectedAccountTelemetry,
    refreshSelectedAccountToken: model.refreshSelectedAccountToken,
    refreshAllAccountsTelemetry: model.refreshAllAccountsTelemetry,
    pauseSelectedAccount: model.pauseSelectedAccount,
    reactivateSelectedAccount: model.reactivateSelectedAccount,
    deleteSelectedAccount: model.deleteSelectedAccount,
  };
}

export function buildLogsContextValue(model: TightropeModel, drawerRow: LogsContextValue['drawerRow']): LogsContextValue {
  return {
    rows: model.state.rows,
    drawerRow,
    openDrawer: model.openDrawer,
    closeDrawer: model.closeDrawer,
  };
}
