import { act, renderHook } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import { useDedupedRefresh } from './useDedupedRefresh';

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

describe('useDedupedRefresh', () => {
  it('deduplicates concurrent calls and returns the same promise', async () => {
    const first = deferred<number>();
    const fn = vi.fn().mockReturnValue(first.promise);
    const { result } = renderHook(() => useDedupedRefresh(fn));

    const promiseA = result.current();
    const promiseB = result.current();

    expect(fn).toHaveBeenCalledTimes(1);
    expect(promiseA).toBe(promiseB);

    first.resolve(42);
    await expect(promiseA).resolves.toBe(42);
  });

  it('starts a new call after the in-flight promise settles', async () => {
    const first = deferred<number>();
    const second = deferred<number>();
    const fn = vi
      .fn<() => Promise<number>>()
      .mockReturnValueOnce(first.promise)
      .mockReturnValueOnce(second.promise);

    const { result } = renderHook(() => useDedupedRefresh(fn));

    const promiseA = result.current();
    first.resolve(1);
    await expect(promiseA).resolves.toBe(1);

    let promiseB!: Promise<number>;
    await act(async () => {
      promiseB = result.current();
    });

    expect(fn).toHaveBeenCalledTimes(2);
    expect(promiseB).not.toBe(promiseA);

    second.resolve(2);
    await expect(promiseB).resolves.toBe(2);
  });
});
