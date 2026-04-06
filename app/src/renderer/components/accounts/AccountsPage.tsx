import { useAccountsContext, useNavigationContext } from '../../state/context';
import { AccountDetailPanel } from './AccountDetailPanel';
import { AccountsSidebar } from './AccountsSidebar';

export function AccountsPage() {
  const navigation = useNavigationContext();
  const accounts = useAccountsContext();

  if (navigation.currentPage !== 'accounts') return null;

  return (
    <section className="accounts-page page active" id="pageAccounts" data-page="accounts">
      <AccountsSidebar
        filteredAccounts={accounts.filteredAccounts}
        totalAccounts={accounts.accounts.length}
        selectedAccountDetail={accounts.selectedAccountDetail}
        accountSearchQuery={accounts.accountSearchQuery}
        accountStatusFilter={accounts.accountStatusFilter}
        onOpenAddAccount={accounts.openAddAccountDialog}
        isRefreshingAllTelemetry={accounts.isRefreshingAllAccountTelemetry}
        onSearch={accounts.setAccountSearchQuery}
        onFilterStatus={accounts.setAccountStatusFilter}
        onSelectDetail={accounts.selectAccountDetail}
        onRefreshAllTelemetry={accounts.refreshAllAccountsTelemetry}
      />
      <AccountDetailPanel
        selectedAccountDetail={accounts.selectedAccountDetail}
        accountUsage24h={accounts.selectedAccountUsage24h}
        stableSparklinePercents={accounts.stableSparklinePercents}
        formatNumber={accounts.formatNumber}
        isRefreshingUsageTelemetry={accounts.isRefreshingSelectedAccountTelemetry}
        isRefreshingToken={accounts.isRefreshingSelectedAccountToken}
        trafficNowMs={accounts.trafficClockMs}
        trafficActiveWindowMs={accounts.trafficActiveWindowMs}
        onRefreshUsageTelemetry={accounts.refreshSelectedAccountTelemetry}
        onRefreshToken={accounts.refreshSelectedAccountToken}
        onPauseAccount={accounts.pauseSelectedAccount}
        onReactivateAccount={accounts.reactivateSelectedAccount}
        onDeleteAccount={accounts.deleteSelectedAccount}
      />
    </section>
  );
}
