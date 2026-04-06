import { useCallback, type Dispatch, type SetStateAction } from 'react';
import type { AppRuntimeState } from '../../shared/types';
import { nowStamp } from '../logic';
import { publishStatusNotice, type StatusNoticeLevel } from '../statusNotices';

interface UseRuntimeEventsOptions {
  setState: Dispatch<SetStateAction<AppRuntimeState>>;
}

interface UseRuntimeEventsResult {
  pushRuntimeEvent: (text: string, level?: StatusNoticeLevel) => void;
}

export function useRuntimeEvents(options: UseRuntimeEventsOptions): UseRuntimeEventsResult {
  const { setState } = options;

  const pushRuntimeEvent = useCallback((text: string, level: StatusNoticeLevel = 'info'): void => {
    setState((previous) => ({
      ...previous,
      runtimeState: {
        ...previous.runtimeState,
        events: [`${nowStamp()} ${text}`, ...previous.runtimeState.events].slice(0, 12),
      },
    }));
    publishStatusNotice({ message: text, level });
  }, [setState]);

  return { pushRuntimeEvent };
}
