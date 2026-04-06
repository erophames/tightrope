import { describe, expect, test } from 'vitest';
import {
  getCurrentStatusNotice,
  publishStatusProgressNotice,
  publishStatusNotice,
  resetStatusNoticesForTests,
  subscribeStatusNotice,
} from './statusNotices';

describe('statusNotices', () => {
  test('starts with ready status', () => {
    resetStatusNoticesForTests();
    expect(getCurrentStatusNotice().message).toBe('Ready');
    expect(getCurrentStatusNotice().level).toBe('info');
  });

  test('notifies subscribers and respects unsubscribe', () => {
    resetStatusNoticesForTests();
    const seen: string[] = [];
    const unsubscribe = subscribeStatusNotice((notice) => {
      seen.push(notice.message);
    });

    publishStatusNotice({ message: 'backend started', level: 'success' });
    unsubscribe();
    publishStatusNotice({ message: 'backend stopped', level: 'warn' });

    expect(seen).toEqual(['Ready', 'backend started']);
  });

  test('supports progress-mode notices', () => {
    resetStatusNoticesForTests();
    publishStatusProgressNotice({
      label: 'Refreshing accounts telemetry',
      current: 3,
      total: 10,
    });

    const current = getCurrentStatusNotice();
    expect(current.renderMode).toBe('progress');
    expect(current.progress).toEqual({
      label: 'Refreshing accounts telemetry',
      current: 3,
      total: 10,
    });
    expect(current.message).toContain('3/10');
  });
});
