import { act, renderHook } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import { useAsyncAction } from './useAsyncAction';

type Deferred<T> = {
  promise: Promise<T>;
  resolve: (value: T) => void;
  reject: (error: unknown) => void;
};

function deferred<T>(): Deferred<T> {
  let resolve!: (value: T) => void;
  let reject!: (error: unknown) => void;
  const promise = new Promise<T>((res, rej) => {
    resolve = res;
    reject = rej;
  });
  return { promise, resolve, reject };
}

describe('useAsyncAction', () => {
  it('tracks loading while request is in-flight', async () => {
    const pending = deferred<void>();
    const fn = vi.fn<(id: string) => Promise<void>>().mockReturnValue(pending.promise);

    const { result } = renderHook(() => useAsyncAction(fn, [fn]));

    let runPromise!: Promise<void>;
    await act(async () => {
      runPromise = result.current.execute('acc_1');
    });

    expect(result.current.loading).toBe(true);
    expect(result.current.error).toBeNull();
    expect(fn).toHaveBeenCalledWith('acc_1');

    pending.resolve();
    await act(async () => {
      await runPromise;
    });

    expect(result.current.loading).toBe(false);
    expect(result.current.error).toBeNull();
  });

  it('stores errors and clears them on the next run', async () => {
    const firstError = new Error('boom');
    const fn = vi
      .fn<() => Promise<void>>()
      .mockRejectedValueOnce(firstError)
      .mockResolvedValueOnce(undefined);

    const { result } = renderHook(() => useAsyncAction(fn, [fn]));

    await act(async () => {
      await expect(result.current.execute()).rejects.toThrow('boom');
    });

    expect(result.current.loading).toBe(false);
    expect(result.current.error?.message).toBe('boom');

    await act(async () => {
      await expect(result.current.execute()).resolves.toBeUndefined();
    });

    expect(result.current.error).toBeNull();
  });
});
