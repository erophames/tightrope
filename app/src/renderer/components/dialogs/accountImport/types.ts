export type AccountImportDeltaKind = 'new' | 'update' | 'skip';

export interface AccountImportDeltaRow {
  id: string;
  account: string;
  plan: string;
  login: string;
  delta: AccountImportDeltaKind;
  action: string;
}

export interface AccountImportSummaryStats {
  totalRows: number;
  newRows: number;
  updateRows: number;
  skipRows: number;
}
