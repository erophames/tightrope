import { act, renderHook } from '@testing-library/react';
import { useState } from 'react';
import { describe, expect, it, vi } from 'vitest';
import { createInitialRuntimeState } from '../../test/fixtures/seed';
import type { AppPage, AppRuntimeState } from '../../shared/types';
import { useNavigation } from './useNavigation';

interface SetupOptions {
  currentPage?: AppPage;
  hasUnsavedSettingsChanges?: boolean;
  settingsSaveInFlight?: boolean;
  saveDashboardSettingsResult?: boolean;
}

function setup(options: SetupOptions = {}) {
  const {
    currentPage = 'settings',
    hasUnsavedSettingsChanges = true,
    settingsSaveInFlight = false,
    saveDashboardSettingsResult = true,
  } = options;
  const discardDashboardSettingsChanges = vi.fn();
  const saveDashboardSettings = vi.fn().mockResolvedValue(saveDashboardSettingsResult);

  const hook = renderHook(() => {
    const [state, setState] = useState<AppRuntimeState>({
      ...createInitialRuntimeState(),
      currentPage,
    });

    const navigation = useNavigation({
      currentPage: state.currentPage,
      hasUnsavedSettingsChanges,
      settingsSaveInFlight,
      setState,
      discardDashboardSettingsChanges,
      saveDashboardSettings,
    });

    return {
      state,
      discardDashboardSettingsChanges,
      saveDashboardSettings,
      ...navigation,
    };
  });

  return { ...hook, discardDashboardSettingsChanges, saveDashboardSettings };
}

describe('useNavigation', () => {
  it('intercepts navigation away from settings when there are unsaved changes', () => {
    const { result } = setup();

    act(() => {
      result.current.setCurrentPage('logs');
    });

    expect(result.current.state.currentPage).toBe('settings');
    expect(result.current.settingsLeaveTargetPage).toBe('logs');
    expect(result.current.settingsLeaveDialogOpen).toBe(true);
  });

  it('discards settings and navigates to pending target page', () => {
    const { result, discardDashboardSettingsChanges } = setup();

    act(() => {
      result.current.setCurrentPage('logs');
    });

    act(() => {
      result.current.discardSettingsAndNavigate();
    });

    expect(discardDashboardSettingsChanges).toHaveBeenCalledTimes(1);
    expect(result.current.state.currentPage).toBe('logs');
    expect(result.current.settingsLeaveDialogOpen).toBe(false);
  });

  it('saves settings before navigating to pending target page', async () => {
    const { result, saveDashboardSettings } = setup();

    act(() => {
      result.current.setCurrentPage('accounts');
    });

    await act(async () => {
      await result.current.saveSettingsAndNavigate();
    });

    expect(saveDashboardSettings).toHaveBeenCalledTimes(1);
    expect(result.current.state.currentPage).toBe('accounts');
    expect(result.current.settingsLeaveDialogOpen).toBe(false);
  });

  it('keeps leave dialog open when save fails', async () => {
    const { result } = setup({ saveDashboardSettingsResult: false });

    act(() => {
      result.current.setCurrentPage('sessions');
    });

    await act(async () => {
      await result.current.saveSettingsAndNavigate();
    });

    expect(result.current.state.currentPage).toBe('settings');
    expect(result.current.settingsLeaveDialogOpen).toBe(true);
  });

  it('does not close leave dialog while settings save is in flight', () => {
    const { result } = setup({ settingsSaveInFlight: true });

    act(() => {
      result.current.setCurrentPage('accounts');
    });

    act(() => {
      result.current.closeSettingsLeaveDialog();
    });

    expect(result.current.settingsLeaveDialogOpen).toBe(true);
  });
});
