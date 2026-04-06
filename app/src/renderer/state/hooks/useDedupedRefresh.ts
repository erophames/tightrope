import { useCallback, useRef } from 'react';

export function useDedupedRefresh<T>(fn: () => Promise<T>): () => Promise<T> {
  const fnRef = useRef(fn);
  const inflightRef = useRef<Promise<T> | null>(null);

  fnRef.current = fn;

  return useCallback(() => {
    if (inflightRef.current) {
      return inflightRef.current;
    }

    const promise = fnRef.current().finally(() => {
      inflightRef.current = null;
    });

    inflightRef.current = promise;
    return promise;
  }, []);
}
