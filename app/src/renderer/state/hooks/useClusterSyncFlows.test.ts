import { describe, expect, it } from 'vitest';
import { defaultDashboardSettings } from './useSettings';
import { clusterConfigFromSettings } from './useClusterSyncFlows';

describe('clusterConfigFromSettings', () => {
  it('maps conflict strategy and journal retention settings into native cluster config', () => {
    const settings = {
      ...defaultDashboardSettings,
      syncConflictResolution: 'site_priority' as const,
      syncJournalRetentionDays: 45,
    };

    const config = clusterConfigFromSettings(settings, ['10.0.0.9:9400']);

    expect(config.conflict_resolution).toBe('site_priority');
    expect(config.journal_retention_days).toBe(45);
    expect(config.manual_peers).toEqual(['10.0.0.9:9400']);
  });
});
