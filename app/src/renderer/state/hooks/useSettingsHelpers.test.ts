import { describe, expect, test } from 'vitest';
import type { DashboardSettings } from '../../shared/types';
import {
  DASHBOARD_SETTINGS_UPDATE_KEYS,
  SYNC_SETTINGS_FINGERPRINT_KEYS,
  boundedInteger,
  buildDashboardSettingsUpdate,
  buildSyncSettingsFingerprint,
} from './useSettingsHelpers';
import { defaultDashboardSettings } from './useSettings';

describe('useSettingsHelpers', () => {
  test('buildDashboardSettingsUpdate includes patchable keys and omits non-patchable fields', () => {
    const settings: DashboardSettings = {
      ...defaultDashboardSettings,
      totpConfigured: true,
      routingStrategy: 'round_robin',
      syncPort: 9555,
    };

    const update = buildDashboardSettingsUpdate(settings);
    const keys = Object.keys(update).sort();

    expect(keys).toEqual([...DASHBOARD_SETTINGS_UPDATE_KEYS].sort());
    expect('totpConfigured' in update).toBe(false);
    expect(update.routingStrategy).toBe('round_robin');
    expect(update.syncPort).toBe(9555);
  });

  test('buildSyncSettingsFingerprint only changes when sync fields change', () => {
    const base = { ...defaultDashboardSettings };
    const withNonSyncChange = { ...base, theme: 'dark' as const };
    const withSyncChange = { ...base, syncPort: base.syncPort + 1 };

    const baseFingerprint = buildSyncSettingsFingerprint(base);
    const nonSyncFingerprint = buildSyncSettingsFingerprint(withNonSyncChange);
    const syncFingerprint = buildSyncSettingsFingerprint(withSyncChange);

    expect(nonSyncFingerprint).toBe(baseFingerprint);
    expect(syncFingerprint).not.toBe(baseFingerprint);
    expect(SYNC_SETTINGS_FINGERPRINT_KEYS).toContain('syncPort');
    expect(SYNC_SETTINGS_FINGERPRINT_KEYS).not.toContain('theme');
  });

  test('boundedInteger truncates and clamps values', () => {
    expect(boundedInteger(9.9, 1, 10)).toBe(9);
    expect(boundedInteger(-100, 1, 10)).toBe(1);
    expect(boundedInteger(999, 1, 10)).toBe(10);
  });
});
