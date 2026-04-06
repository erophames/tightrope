import { act, renderHook } from '@testing-library/react';
import { useState } from 'react';
import { describe, expect, it, vi } from 'vitest';
import { createInitialRuntimeState } from '../../test/fixtures/seed';
import type { AppRuntimeState, DashboardSettings } from '../../shared/types';
import { useSettings } from './useSettings';

function makeSettings(overrides: Partial<DashboardSettings> = {}): DashboardSettings {
  return {
    theme: 'auto',
    stickyThreadsEnabled: false,
    upstreamStreamTransport: 'default',
    preferEarlierResetAccounts: false,
    routingStrategy: 'weighted_round_robin',
    strictLockPoolContinuations: false,
    lockedRoutingAccountIds: [],
    openaiCacheAffinityMaxAgeSeconds: 300,
    importWithoutOverwrite: false,
    totpRequiredOnLogin: false,
    totpConfigured: false,
    apiKeyAuthEnabled: false,
    routingHeadroomWeightPrimary: 0.35,
    routingHeadroomWeightSecondary: 0.65,
    routingScoreAlpha: 0.3,
    routingScoreBeta: 0.25,
    routingScoreGamma: 0.2,
    routingScoreDelta: 0.2,
    routingScoreZeta: 0.05,
    routingScoreEta: 1,
    routingSuccessRateRho: 2,
    routingPlanModelPricingUsdPerMillion: '',
    syncClusterName: 'default',
    syncSiteId: 1,
    syncPort: 9400,
    syncDiscoveryEnabled: true,
    syncIntervalSeconds: 5,
    syncConflictResolution: 'lww',
    syncJournalRetentionDays: 30,
    syncTlsEnabled: true,
    syncRequireHandshakeAuth: true,
    syncClusterSharedSecret: '',
    syncTlsVerifyPeer: true,
    syncTlsCaCertificatePath: '',
    syncTlsCertificateChainPath: '',
    syncTlsPrivateKeyPath: '',
    syncTlsPinnedPeerCertificateSha256: '',
    syncSchemaVersion: 1,
    syncMinSupportedSchemaVersion: 1,
    syncAllowSchemaDowngrade: false,
    syncPeerProbeEnabled: true,
    syncPeerProbeIntervalMs: 5000,
    syncPeerProbeTimeoutMs: 500,
    syncPeerProbeMaxPerRefresh: 2,
    syncPeerProbeFailClosed: true,
    syncPeerProbeFailClosedFailures: 3,
    ...overrides,
  };
}

describe('useSettings', () => {
  it('tracks dirty settings state after patching values', () => {
    const pushRuntimeEvent = vi.fn();
    const getSettingsRequest = vi.fn(async () => null);
    const updateSettingsRequest = vi.fn(async () => null);

    const { result } = renderHook(() => {
      const [state, setState] = useState<AppRuntimeState>(createInitialRuntimeState());
      const settings = useSettings({
        stateTheme: state.theme,
        stateRoutingMode: state.routingMode,
        setState,
        pushRuntimeEvent,
        getSettingsRequest,
        updateSettingsRequest,
      });
      return { state, ...settings };
    });

    expect(result.current.hasUnsavedSettingsChanges).toBe(false);

    act(() => {
      result.current.setTheme('dark');
    });

    expect(result.current.dashboardSettings.theme).toBe('dark');
    expect(result.current.hasUnsavedSettingsChanges).toBe(true);
  });

  it('saves settings and syncs applied + runtime state', async () => {
    const pushRuntimeEvent = vi.fn();
    const initial = makeSettings();
    const getSettingsRequest = vi.fn(async () => initial);
    const updateSettingsRequest = vi.fn(async (update) => ({ ...initial, ...update }));

    const { result } = renderHook(() => {
      const [state, setState] = useState<AppRuntimeState>(createInitialRuntimeState());
      const settings = useSettings({
        stateTheme: state.theme,
        stateRoutingMode: state.routingMode,
        setState,
        pushRuntimeEvent,
        getSettingsRequest,
        updateSettingsRequest,
      });
      return { state, ...settings };
    });

    act(() => {
      result.current.setTheme('dark');
      result.current.setRoutingMode('round_robin');
    });

    await act(async () => {
      await result.current.saveDashboardSettings();
    });

    expect(result.current.appliedDashboardSettings.theme).toBe('dark');
    expect(result.current.state.theme).toBe('dark');
    expect(result.current.state.routingMode).toBe('round_robin');
    expect(pushRuntimeEvent).toHaveBeenCalledWith('settings saved', 'success');
  });
});
