import { act, renderHook, waitFor } from '@testing-library/react';
import { useState } from 'react';
import { describe, expect, it, vi } from 'vitest';
import { createInitialRuntimeState } from '../../test/fixtures/seed';
import type { AppRuntimeState } from '../../shared/types';
import { useRuntimeState } from './useRuntimeState';

describe('useRuntimeState', () => {
  it('refreshes backend runtime state', async () => {
    const pushRuntimeEvent = vi.fn();
    const backendStatusRequest = vi.fn().mockResolvedValue({ enabled: false });
    const backendStartRequest = vi.fn().mockResolvedValue({ enabled: true });
    const backendStopRequest = vi.fn().mockResolvedValue({ enabled: false });

    const { result } = renderHook(() => {
      const [state, setState] = useState<AppRuntimeState>(createInitialRuntimeState());
      const runtime = useRuntimeState({
        runtimeState: state.runtimeState,
        setState,
        pushRuntimeEvent,
        backendStatusRequest,
        backendStartRequest,
        backendStopRequest,
      });
      return { state, ...runtime };
    });

    await act(async () => {
      await result.current.refreshBackendState();
    });

    expect(result.current.state.runtimeState.backend).toBe('stopped');
    expect(result.current.state.runtimeState.health).toBe('warn');
  });

  it('performs restart action and clears paused routes', async () => {
    const pushRuntimeEvent = vi.fn();
    const backendStatusRequest = vi.fn().mockResolvedValue({ enabled: true });
    const backendStartRequest = vi.fn().mockResolvedValue({ enabled: true });
    const backendStopRequest = vi.fn().mockResolvedValue({ enabled: false });

    const { result } = renderHook(() => {
      const [state, setState] = useState<AppRuntimeState>({
        ...createInitialRuntimeState(),
        runtimeState: {
          ...createInitialRuntimeState().runtimeState,
          pausedRoutes: true,
        },
      });

      const runtime = useRuntimeState({
        runtimeState: state.runtimeState,
        setState,
        pushRuntimeEvent,
        backendStatusRequest,
        backendStartRequest,
        backendStopRequest,
      });
      return { state, ...runtime };
    });

    act(() => {
      result.current.setRuntimeAction('restart');
    });

    await waitFor(() => {
      expect(result.current.state.runtimeState.backend).toBe('running');
      expect(result.current.state.runtimeState.pausedRoutes).toBe(false);
      expect(pushRuntimeEvent).toHaveBeenCalledWith('backend restarted', 'success');
    });
  });

  it('blocks pause toggle while backend is stopped', () => {
    const pushRuntimeEvent = vi.fn();
    const backendStatusRequest = vi.fn().mockResolvedValue({ enabled: false });
    const backendStartRequest = vi.fn().mockResolvedValue({ enabled: true });
    const backendStopRequest = vi.fn().mockResolvedValue({ enabled: false });

    const { result } = renderHook(() => {
      const [state, setState] = useState<AppRuntimeState>({
        ...createInitialRuntimeState(),
        runtimeState: {
          ...createInitialRuntimeState().runtimeState,
          backend: 'stopped',
        },
      });
      const runtime = useRuntimeState({
        runtimeState: state.runtimeState,
        setState,
        pushRuntimeEvent,
        backendStatusRequest,
        backendStartRequest,
        backendStopRequest,
      });
      return { state, ...runtime };
    });

    act(() => {
      result.current.toggleRoutePause();
    });

    expect(result.current.state.runtimeState.pausedRoutes).toBe(false);
    expect(pushRuntimeEvent).toHaveBeenCalledWith('pause ignored: backend is stopped', 'warn');
  });

  it('toggles auto-restart flag and publishes runtime notice', () => {
    const pushRuntimeEvent = vi.fn();
    const backendStatusRequest = vi.fn().mockResolvedValue({ enabled: false });
    const backendStartRequest = vi.fn().mockResolvedValue({ enabled: true });
    const backendStopRequest = vi.fn().mockResolvedValue({ enabled: false });

    const { result } = renderHook(() => {
      const [state, setState] = useState<AppRuntimeState>(createInitialRuntimeState());
      const runtime = useRuntimeState({
        runtimeState: state.runtimeState,
        setState,
        pushRuntimeEvent,
        backendStatusRequest,
        backendStartRequest,
        backendStopRequest,
      });
      return { state, ...runtime };
    });

    act(() => {
      result.current.toggleAutoRestart();
    });

    expect(result.current.state.runtimeState.autoRestart).toBe(false);
    expect(pushRuntimeEvent).toHaveBeenCalledWith('auto-restart disabled');
  });
});
