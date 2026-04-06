import type { StatusNoticeLevel } from './statusNotices';

export interface RuntimeEventReporter {
  (text: string, level?: StatusNoticeLevel): void;
}

export function errorMessage(error: unknown, fallback: string): string {
  return error instanceof Error ? error.message : fallback;
}

export function reportWarn(reporter: RuntimeEventReporter, error: unknown, fallback: string): string {
  const message = errorMessage(error, fallback);
  reporter(message, 'warn');
  return message;
}
