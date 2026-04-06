import type { AddAccountStep, AppRuntimeState } from '../../shared/types';

export interface OAuthFlowReducerState {
  phase: AddAccountStep;
  errorMessage: string;
}

export type OAuthFlowReducerAction =
  | { type: 'set_phase'; phase: AddAccountStep }
  | { type: 'fail'; message: string }
  | { type: 'reset' };

export function createInitialOAuthFlowReducerState(state: AppRuntimeState): OAuthFlowReducerState {
  return {
    phase: state.addAccountStep,
    errorMessage: state.addAccountError,
  };
}

export function oauthFlowReducer(state: OAuthFlowReducerState, action: OAuthFlowReducerAction): OAuthFlowReducerState {
  switch (action.type) {
    case 'set_phase':
      return {
        phase: action.phase,
        errorMessage: state.errorMessage,
      };
    case 'fail':
      return {
        phase: 'stepError',
        errorMessage: action.message,
      };
    case 'reset':
      return {
        phase: 'stepMethod',
        errorMessage: 'Something went wrong.',
      };
    default:
      return state;
  }
}
