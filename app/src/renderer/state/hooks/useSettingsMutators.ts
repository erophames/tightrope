import type { DashboardSettings, DashboardSettingsUpdate } from '../../shared/types';
import { boundedInteger } from './useSettingsHelpers';

export function clampUnitInterval(value: number): number {
  return Math.max(0, Math.min(1, value));
}

export function clampStrategyRho(value: number): number {
  return Math.min(10, Math.max(0.1, value));
}

export function scoringWeightKeyToField(
  key: 'alpha' | 'beta' | 'gamma' | 'delta' | 'zeta' | 'eta',
): keyof DashboardSettingsUpdate {
  const keyMap: Record<typeof key, keyof DashboardSettingsUpdate> = {
    alpha: 'routingScoreAlpha',
    beta: 'routingScoreBeta',
    gamma: 'routingScoreGamma',
    delta: 'routingScoreDelta',
    zeta: 'routingScoreZeta',
    eta: 'routingScoreEta',
  };
  return keyMap[key];
}

export function headroomWeightKeyToField(key: 'wp' | 'ws'): keyof DashboardSettingsUpdate {
  return key === 'wp' ? 'routingHeadroomWeightPrimary' : 'routingHeadroomWeightSecondary';
}

export function normalizeSyncClusterName(clusterName: string): string {
  const normalized = clusterName.trim();
  return normalized === '' ? 'default' : normalized;
}

export function buildSyncSchemaVersionPatch(
  settings: DashboardSettings,
  version: number,
): Pick<DashboardSettingsUpdate, 'syncSchemaVersion' | 'syncMinSupportedSchemaVersion'> {
  const bounded = boundedInteger(version, 1, 1_000_000);
  return {
    syncSchemaVersion: bounded,
    syncMinSupportedSchemaVersion: Math.min(settings.syncMinSupportedSchemaVersion, bounded),
  };
}

export function buildSyncMinSupportedSchemaVersionPatch(
  settings: DashboardSettings,
  version: number,
): Pick<DashboardSettingsUpdate, 'syncMinSupportedSchemaVersion'> {
  const bounded = boundedInteger(version, 1, 1_000_000);
  return {
    syncMinSupportedSchemaVersion: Math.min(bounded, settings.syncSchemaVersion),
  };
}
