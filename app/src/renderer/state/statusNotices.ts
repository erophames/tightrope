import { nowStamp } from './logic';

export type StatusNoticeLevel = 'info' | 'success' | 'warn' | 'error';
export type StatusNoticeRenderMode = 'text' | 'progress';

export interface StatusNoticeProgress {
  label: string;
  current: number;
  total: number;
}

export interface StatusNotice {
  sequence: number;
  message: string;
  level: StatusNoticeLevel;
  at: string;
  renderMode: StatusNoticeRenderMode;
  progress: StatusNoticeProgress | null;
}

export interface PublishStatusNoticeInput {
  message: string;
  level?: StatusNoticeLevel;
  renderMode?: StatusNoticeRenderMode;
  progress?: StatusNoticeProgress | null;
}

export interface PublishStatusProgressInput {
  label: string;
  current: number;
  total: number;
  level?: StatusNoticeLevel;
  message?: string;
}

type StatusNoticeListener = (notice: StatusNotice) => void;

const listeners = new Set<StatusNoticeListener>();

let sequenceCounter = 1;
let currentStatusNotice: StatusNotice = {
  sequence: 0,
  message: 'Ready',
  level: 'info',
  at: nowStamp(),
  renderMode: 'text',
  progress: null,
};

export function getCurrentStatusNotice(): StatusNotice {
  return currentStatusNotice;
}

export function subscribeStatusNotice(listener: StatusNoticeListener): () => void {
  listeners.add(listener);
  listener(currentStatusNotice);
  return () => {
    listeners.delete(listener);
  };
}

export function publishStatusNotice(input: PublishStatusNoticeInput | string): StatusNotice {
  const payload: PublishStatusNoticeInput = typeof input === 'string' ? { message: input } : input;
  const renderMode = payload.renderMode ?? 'text';
  const progress = renderMode === 'progress' ? payload.progress ?? null : null;
  currentStatusNotice = {
    sequence: sequenceCounter++,
    message: payload.message,
    level: payload.level ?? 'info',
    at: nowStamp(),
    renderMode,
    progress,
  };

  listeners.forEach((listener) => listener(currentStatusNotice));
  return currentStatusNotice;
}

export function publishStatusProgressNotice(input: PublishStatusProgressInput): StatusNotice {
  const total = Number.isFinite(input.total) ? Math.max(1, Math.trunc(input.total)) : 1;
  const current = Number.isFinite(input.current) ? Math.max(0, Math.min(total, Math.trunc(input.current))) : 0;
  const label = input.label.trim() || 'Progress';
  const message = input.message?.trim() || `${label} ${current}/${total}`;

  return publishStatusNotice({
    message,
    level: input.level ?? 'info',
    renderMode: 'progress',
    progress: {
      label,
      current,
      total,
    },
  });
}

export function resetStatusNoticesForTests(): void {
  sequenceCounter = 1;
  currentStatusNotice = {
    sequence: 0,
    message: 'Ready',
    level: 'info',
    at: nowStamp(),
    renderMode: 'text',
    progress: null,
  };
  listeners.clear();
}
