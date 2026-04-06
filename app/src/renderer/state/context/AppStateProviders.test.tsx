import { render, screen, waitFor } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { memo } from 'react';
import { describe, expect, test, vi } from 'vitest';
import type { DashboardSettingsUpdate } from '../../shared/types';
import { createTightropeService, type TightropeService } from '../../services/tightrope';
import { makeTestService } from '../../test/makeService';
import { AppStateProviders } from './AppStateProviders';
import { useAccountsContext } from './AccountsContext';
import { useNavigationContext } from './NavigationContext';
import { useSettingsContext } from './SettingsContext';

function ContextProbe() {
  const navigation = useNavigationContext();
  const settings = useSettingsContext();
  const accounts = useAccountsContext();

  return (
    <div>
      <div data-testid="current-page">{navigation.currentPage}</div>
      <div data-testid="leave-dialog-open">{navigation.settingsLeaveDialogOpen ? 'open' : 'closed'}</div>
      <div data-testid="settings-theme">{settings.theme}</div>
      <div data-testid="settings-dirty">{settings.settingsDirty ? 'true' : 'false'}</div>
      <div data-testid="accounts-total">{String(accounts.accounts.length)}</div>
      <div data-testid="filtered-accounts-total">{String(accounts.filteredAccounts.length)}</div>
      <div data-testid="selected-account-id">{accounts.selectedAccountDetail?.id ?? ''}</div>

      <button type="button" onClick={() => navigation.setCurrentPage('settings')}>
        go-settings
      </button>
      <button type="button" onClick={() => navigation.setCurrentPage('accounts')}>
        go-accounts
      </button>
      <button type="button" onClick={() => navigation.discardSettingsAndNavigate()}>
        discard-and-navigate
      </button>

      <button type="button" onClick={() => settings.setTheme('dark')}>
        set-theme-dark
      </button>
      <button type="button" onClick={() => settings.discardSettings()}>
        discard-settings
      </button>
      <button type="button" onClick={() => settings.saveSettings()}>
        save-settings
      </button>

      <button type="button" onClick={() => accounts.selectAccountDetail('acc-test')}>
        select-account
      </button>
      <button
        type="button"
        onClick={() => {
          void accounts.pauseSelectedAccount();
        }}
      >
        pause-selected
      </button>
      <button
        type="button"
        onClick={() => {
          void accounts.reactivateSelectedAccount();
        }}
      >
        reactivate-selected
      </button>
      <button
        type="button"
        onClick={() => {
          void accounts.deleteSelectedAccount();
        }}
      >
        delete-selected
      </button>
      <button type="button" onClick={() => accounts.setAccountSearchQuery('no-match')}>
        search-no-match
      </button>
    </div>
  );
}

function renderProviders(service?: TightropeService) {
  return render(
    <AppStateProviders service={service}>
      <ContextProbe />
    </AppStateProviders>,
  );
}

describe('AppStateProviders', () => {
  test('wires navigation + settings dirty guard across contexts', async () => {
    const user = userEvent.setup();
    renderProviders();

    await waitFor(() => expect(screen.getByTestId('settings-theme')).toHaveTextContent('auto'));
    expect(screen.getByTestId('current-page')).toHaveTextContent('router');

    await user.click(screen.getByRole('button', { name: 'go-settings' }));
    expect(screen.getByTestId('current-page')).toHaveTextContent('settings');

    await user.click(screen.getByRole('button', { name: 'set-theme-dark' }));
    await waitFor(() => expect(screen.getByTestId('settings-theme')).toHaveTextContent('dark'));
    await waitFor(() => expect(screen.getByTestId('settings-dirty')).toHaveTextContent('true'));

    await user.click(screen.getByRole('button', { name: 'go-accounts' }));
    await waitFor(() => expect(screen.getByTestId('leave-dialog-open')).toHaveTextContent('open'));
    expect(screen.getByTestId('current-page')).toHaveTextContent('settings');

    await user.click(screen.getByRole('button', { name: 'discard-and-navigate' }));
    await waitFor(() => expect(screen.getByTestId('current-page')).toHaveTextContent('accounts'));
    await waitFor(() => expect(screen.getByTestId('leave-dialog-open')).toHaveTextContent('closed'));
    await waitFor(() => expect(screen.getByTestId('settings-theme')).toHaveTextContent('auto'));
    await waitFor(() => expect(screen.getByTestId('settings-dirty')).toHaveTextContent('false'));
  });

  test('wires settings save action to native settings update', async () => {
    const user = userEvent.setup();
    const baselineService = createTightropeService();
    const baselineSettings = await baselineService.getSettingsRequest();
    expect(baselineSettings).not.toBeNull();
    const settings = baselineSettings as NonNullable<typeof baselineSettings>;
    const updateSettings = vi.fn(async (update: DashboardSettingsUpdate) => {
      return {
        ...settings,
        ...update,
      };
    });
    const service = makeTestService({
      updateSettingsRequest: updateSettings,
    });

    renderProviders(service);

    await waitFor(() => expect(screen.getByTestId('settings-theme')).toHaveTextContent('auto'));
    await user.click(screen.getByRole('button', { name: 'go-settings' }));
    await user.click(screen.getByRole('button', { name: 'set-theme-dark' }));
    await waitFor(() => expect(screen.getByTestId('settings-dirty')).toHaveTextContent('true'));

    await user.click(screen.getByRole('button', { name: 'save-settings' }));

    await waitFor(() => expect(updateSettings).toHaveBeenCalled());
    expect(updateSettings).toHaveBeenLastCalledWith(expect.objectContaining({ theme: 'dark' }));
    await waitFor(() => expect(screen.getByTestId('settings-dirty')).toHaveTextContent('false'));
  });

  test('wires account actions and derived filtering through accounts context', async () => {
    const user = userEvent.setup();
    const pauseAccount = vi.fn(async () => {});
    const reactivateAccount = vi.fn(async () => {});
    const deleteAccount = vi.fn(async () => {});
    const service = makeTestService({
      listAccountsRequest: vi.fn(async () => [
        {
          accountId: 'acc-test',
          email: 'tester@test.local',
          provider: 'openai',
          status: 'active',
        },
      ]),
      pauseAccountRequest: pauseAccount,
      reactivateAccountRequest: reactivateAccount,
      deleteAccountRequest: deleteAccount,
    });

    renderProviders(service);

    await waitFor(() => expect(screen.getByTestId('settings-theme')).toHaveTextContent('auto'));
    await waitFor(() => expect(screen.getByTestId('accounts-total')).toHaveTextContent('1'));
    await waitFor(() => expect(screen.getByTestId('filtered-accounts-total')).toHaveTextContent('1'));

    await user.click(screen.getByRole('button', { name: 'select-account' }));
    await waitFor(() => expect(screen.getByTestId('selected-account-id')).toHaveTextContent('acc-test'));

    await user.click(screen.getByRole('button', { name: 'pause-selected' }));
    await waitFor(() => expect(pauseAccount).toHaveBeenCalledWith('acc-test'));

    await user.click(screen.getByRole('button', { name: 'reactivate-selected' }));
    await waitFor(() => expect(reactivateAccount).toHaveBeenCalledWith('acc-test'));

    await user.click(screen.getByRole('button', { name: 'search-no-match' }));
    await waitFor(() => expect(screen.getByTestId('filtered-accounts-total')).toHaveTextContent('0'));

    await user.click(screen.getByRole('button', { name: 'delete-selected' }));
    await waitFor(() => expect(deleteAccount).toHaveBeenCalledWith('acc-test'));
    await waitFor(() => expect(screen.getByTestId('selected-account-id')).toHaveTextContent(''));
  });

  test('accepts an injected service override via AppStateProviders service prop', async () => {
    const service = makeTestService({
      listAccountsRequest: vi.fn(async () => [
        {
          accountId: 'svc-1',
          email: 'svc@test.local',
          provider: 'openai',
          status: 'active',
        },
      ]),
    });

    renderProviders(service);

    await waitFor(() => expect(screen.getByTestId('accounts-total')).toHaveTextContent('1'));
    expect(service.listAccountsRequest).toHaveBeenCalled();
  });

  test('settings updates keep navigation slice rerenders bounded and stable', async () => {
    const user = userEvent.setup();
    const navigationRenders = vi.fn();
    const settingsRenders = vi.fn();

    const NavigationSliceProbe = memo(function NavigationSliceProbe() {
      const navigation = useNavigationContext();
      navigationRenders(navigation.currentPage);
      return <div data-testid="nav-slice-page">{navigation.currentPage}</div>;
    });

    const SettingsSliceProbe = memo(function SettingsSliceProbe() {
      const settings = useSettingsContext();
      settingsRenders(settings.theme);
      return (
        <button type="button" onClick={() => settings.setTheme('dark')}>
          set-theme-dark-slice
        </button>
      );
    });

    render(
      <AppStateProviders>
        <NavigationSliceProbe />
        <SettingsSliceProbe />
      </AppStateProviders>,
    );

    await waitFor(() => expect(screen.getByTestId('nav-slice-page')).toHaveTextContent('router'));
    navigationRenders.mockClear();
    settingsRenders.mockClear();

    await user.click(screen.getByRole('button', { name: 'set-theme-dark-slice' }));

    await waitFor(() => expect(settingsRenders).toHaveBeenCalled());
    expect(navigationRenders.mock.calls.length).toBeLessThanOrEqual(1);
    if (navigationRenders.mock.calls.length === 1) {
      expect(navigationRenders).toHaveBeenNthCalledWith(1, 'router');
    }
  });

  test('navigation updates keep settings slice rerenders bounded and stable', async () => {
    const user = userEvent.setup();
    const navigationRenders = vi.fn();
    const settingsRenders = vi.fn();

    const NavigationSliceProbe = memo(function NavigationSliceProbe() {
      const navigation = useNavigationContext();
      navigationRenders(navigation.currentPage);
      return (
        <button type="button" onClick={() => navigation.setCurrentPage('settings')}>
          go-settings-slice
        </button>
      );
    });

    const SettingsSliceProbe = memo(function SettingsSliceProbe() {
      const settings = useSettingsContext();
      settingsRenders(settings.theme);
      return <div data-testid="settings-slice-theme">{settings.theme}</div>;
    });

    render(
      <AppStateProviders>
        <NavigationSliceProbe />
        <SettingsSliceProbe />
      </AppStateProviders>,
    );

    await waitFor(() => expect(screen.getByTestId('settings-slice-theme')).toHaveTextContent('auto'));
    navigationRenders.mockClear();
    settingsRenders.mockClear();

    await user.click(screen.getByRole('button', { name: 'go-settings-slice' }));

    await waitFor(() => expect(navigationRenders).toHaveBeenCalled());
    expect(settingsRenders.mock.calls.length).toBeLessThanOrEqual(1);
    if (settingsRenders.mock.calls.length === 1) {
      expect(settingsRenders).toHaveBeenNthCalledWith(1, 'auto');
    }
  });
});
