import { describe, expect, test } from 'vitest';
import { createInitialRuntimeState } from '../../test/fixtures/seed';
import { createInitialOAuthFlowReducerState, oauthFlowReducer } from './useOAuthFlowReducer';

describe('useOAuthFlowReducer', () => {
  test('createInitialOAuthFlowReducerState seeds phase and error from runtime state', () => {
    const runtimeState = {
      ...createInitialRuntimeState(),
      addAccountStep: 'stepBrowser' as const,
      addAccountError: 'Initial oauth error',
    };

    const state = createInitialOAuthFlowReducerState(runtimeState);

    expect(state.phase).toBe('stepBrowser');
    expect(state.errorMessage).toBe('Initial oauth error');
  });

  test('set_phase updates phase while preserving error message', () => {
    const initial = createInitialOAuthFlowReducerState({
      ...createInitialRuntimeState(),
      addAccountError: 'keep me',
    });

    const next = oauthFlowReducer(initial, { type: 'set_phase', phase: 'stepDevice' });

    expect(next.phase).toBe('stepDevice');
    expect(next.errorMessage).toBe('keep me');
  });

  test('fail transitions to error phase with action message', () => {
    const initial = createInitialOAuthFlowReducerState(createInitialRuntimeState());

    const next = oauthFlowReducer(initial, { type: 'fail', message: 'oauth failed' });

    expect(next.phase).toBe('stepError');
    expect(next.errorMessage).toBe('oauth failed');
  });

  test('reset restores method phase and default error message', () => {
    const initial = oauthFlowReducer(
      createInitialOAuthFlowReducerState(createInitialRuntimeState()),
      { type: 'fail', message: 'oauth failed' },
    );

    const next = oauthFlowReducer(initial, { type: 'reset' });

    expect(next.phase).toBe('stepMethod');
    expect(next.errorMessage).toBe('Something went wrong.');
  });
});
