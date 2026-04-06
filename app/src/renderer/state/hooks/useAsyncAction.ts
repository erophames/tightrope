import { useCallback, useState, type DependencyList } from 'react';

export interface AsyncActionState<TArgs extends unknown[], TResult> {
  execute: (...args: TArgs) => Promise<TResult>;
  loading: boolean;
  error: Error | null;
}

export function useAsyncAction<TArgs extends unknown[], TResult>(
  fn: (...args: TArgs) => Promise<TResult>,
  deps: DependencyList,
): AsyncActionState<TArgs, TResult> {
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<Error | null>(null);

  const execute = useCallback(async (...args: TArgs): Promise<TResult> => {
    setLoading(true);
    setError(null);
    try {
      return await fn(...args);
    } catch (caught) {
      const nextError = caught instanceof Error ? caught : new Error('Async action failed');
      setError(nextError);
      throw nextError;
    } finally {
      setLoading(false);
    }
  }, deps);

  return { execute, loading, error };
}
