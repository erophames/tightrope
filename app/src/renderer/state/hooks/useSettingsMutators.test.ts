import { describe, expect, test } from 'vitest';
import { defaultDashboardSettings } from './useSettings';
import {
  buildSyncMinSupportedSchemaVersionPatch,
  buildSyncSchemaVersionPatch,
  clampStrategyRho,
  clampUnitInterval,
  headroomWeightKeyToField,
  normalizeSyncClusterName,
  scoringWeightKeyToField,
} from './useSettingsMutators';

describe('useSettingsMutators', () => {
  test('clampUnitInterval bounds values to [0, 1]', () => {
    expect(clampUnitInterval(-5)).toBe(0);
    expect(clampUnitInterval(0.5)).toBe(0.5);
    expect(clampUnitInterval(9)).toBe(1);
  });

  test('clampStrategyRho applies expected bounds', () => {
    expect(clampStrategyRho(0)).toBe(0.1);
    expect(clampStrategyRho(1.25)).toBe(1.25);
    expect(clampStrategyRho(500)).toBe(10);
  });

  test('key mapping helpers map scorer/headroom keys to settings fields', () => {
    expect(scoringWeightKeyToField('alpha')).toBe('routingScoreAlpha');
    expect(scoringWeightKeyToField('eta')).toBe('routingScoreEta');
    expect(headroomWeightKeyToField('wp')).toBe('routingHeadroomWeightPrimary');
    expect(headroomWeightKeyToField('ws')).toBe('routingHeadroomWeightSecondary');
  });

  test('normalizeSyncClusterName trims and defaults empty value', () => {
    expect(normalizeSyncClusterName('  alpha  ')).toBe('alpha');
    expect(normalizeSyncClusterName('   ')).toBe('default');
  });

  test('sync schema patch builders preserve schema invariants', () => {
    const settings = {
      ...defaultDashboardSettings,
      syncSchemaVersion: 10,
      syncMinSupportedSchemaVersion: 8,
    };

    expect(buildSyncSchemaVersionPatch(settings, 6)).toEqual({
      syncSchemaVersion: 6,
      syncMinSupportedSchemaVersion: 6,
    });

    expect(buildSyncSchemaVersionPatch(settings, 15)).toEqual({
      syncSchemaVersion: 15,
      syncMinSupportedSchemaVersion: 8,
    });

    expect(buildSyncMinSupportedSchemaVersionPatch(settings, 12)).toEqual({
      syncMinSupportedSchemaVersion: 10,
    });

    expect(buildSyncMinSupportedSchemaVersionPatch(settings, 2)).toEqual({
      syncMinSupportedSchemaVersion: 2,
    });
  });
});
