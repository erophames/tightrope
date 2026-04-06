import { useCallback, useMemo, useState } from 'react';
import { useTightropeService } from '../../../state/context/TightropeServiceContext';
import type {
  SqlImportAction,
  SqlImportApplyResponse,
  SqlImportPreviewResponse,
} from '../../../shared/types';

export type AccountImportStage = 'idle' | 'scanning' | 'ready' | 'importing' | 'done' | 'error';

interface ImportFileDetails {
  fileName: string;
  path: string;
}

interface ImportRequestError extends Error {
  code?: string;
}

function getErrorMessage(error: unknown): string {
  if (error instanceof Error && error.message.trim().length > 0) {
    return error.message;
  }
  return 'Import request failed.';
}

function getErrorCode(error: unknown): string | null {
  if (!(error instanceof Error)) {
    return null;
  }
  const code = (error as ImportRequestError).code;
  return typeof code === 'string' && code.trim().length > 0 ? code : null;
}

function messageIndicatesEncryptedSourceDatabase(message: string): boolean {
  const normalized = message.trim().toLowerCase();
  if (!normalized) {
    return false;
  }
  return normalized.includes('encrypted')
    || normalized.includes('not a database')
    || normalized.includes('source database password');
}

function extractImportFileDetails(file: File): ImportFileDetails | null {
  const candidate = file as File & { path?: string };
  if (typeof candidate.path !== 'string' || candidate.path.length === 0) {
    return null;
  }
  return {
    fileName: file.name,
    path: candidate.path,
  };
}

type OverridesMap = Record<string, SqlImportAction>;

export interface UseAccountImportResult {
  stage: AccountImportStage;
  selectedFileName: string;
  selectedPath: string;
  sourceEncryptionKey: string;
  sourceDatabasePassphrase: string;
  requiresSourceEncryptionKey: boolean;
  requiresSourceDatabasePassphrase: boolean;
  preview: SqlImportPreviewResponse | null;
  applyResult: SqlImportApplyResponse | null;
  error: string | null;
  overrides: OverridesMap;
  importableRows: number;
  handleFileSelected: (file: File | null) => void;
  handleBrowseRequested: () => Promise<void>;
  setSourceEncryptionKey: (value: string) => void;
  setSourceDatabasePassphrase: (value: string) => void;
  setRowOverride: (sourceRowId: string, action: SqlImportAction) => void;
  rescan: () => Promise<void>;
  applyImport: () => Promise<void>;
  reset: () => void;
}

function fileNameFromPath(path: string): string {
  const normalized = path.trim();
  if (!normalized) {
    return '';
  }
  const segments = normalized.split(/[\\/]/);
  return segments[segments.length - 1] ?? normalized;
}

export function useAccountImport(importWithoutOverwrite: boolean): UseAccountImportResult {
  const service = useTightropeService();
  const [stage, setStage] = useState<AccountImportStage>('idle');
  const [selectedFileName, setSelectedFileName] = useState('');
  const [selectedPath, setSelectedPath] = useState('');
  const [sourceEncryptionKey, setSourceEncryptionKey] = useState('');
  const [sourceDatabasePassphrase, setSourceDatabasePassphrase] = useState('');
  const [requiresSourceDatabasePassphrase, setRequiresSourceDatabasePassphrase] = useState(false);
  const [preview, setPreview] = useState<SqlImportPreviewResponse | null>(null);
  const [applyResult, setApplyResult] = useState<SqlImportApplyResponse | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [overrides, setOverrides] = useState<OverridesMap>({});

  const runPreview = useCallback(async (
    sourcePath: string,
    sourceKeyValue?: string,
    sourceDatabasePassphraseValue?: string,
  ): Promise<void> => {
    setStage('scanning');
    setError(null);
    setApplyResult(null);
    try {
      const normalizedSourceKey = (sourceKeyValue ?? sourceEncryptionKey).trim();
      const normalizedSourceDatabasePassphrase = (sourceDatabasePassphraseValue ?? sourceDatabasePassphrase).trim();
      const response = await service.previewSqlImportRequest({
        sourcePath,
        sourceEncryptionKey: normalizedSourceKey.length > 0 ? normalizedSourceKey : undefined,
        sourceDatabasePassphrase:
          normalizedSourceDatabasePassphrase.length > 0 ? normalizedSourceDatabasePassphrase : undefined,
        importWithoutOverwrite,
      });
      setPreview(response);
      setOverrides({});
      setRequiresSourceDatabasePassphrase((previous) => previous || response.requiresSourceDatabasePassphrase === true);
      setStage('ready');
    } catch (previewError) {
      const errorCode = getErrorCode(previewError);
      const message = getErrorMessage(previewError);
      if (
        errorCode === 'source_db_passphrase_required'
        || errorCode === 'source_db_passphrase_invalid'
        || messageIndicatesEncryptedSourceDatabase(message)
      ) {
        setRequiresSourceDatabasePassphrase(true);
      }
      setStage('error');
      setPreview(null);
      setError(message);
    }
  }, [importWithoutOverwrite, service, sourceDatabasePassphrase, sourceEncryptionKey]);

  const handleFileSelected = useCallback((file: File | null) => {
    if (!file) {
      setSelectedFileName('');
      setSelectedPath('');
      setSourceEncryptionKey('');
      setSourceDatabasePassphrase('');
      setRequiresSourceDatabasePassphrase(false);
      setPreview(null);
      setApplyResult(null);
      setError(null);
      setOverrides({});
      setStage('idle');
      return;
    }

    const details = extractImportFileDetails(file);
    setSelectedFileName(file.name);
    setSourceEncryptionKey('');
    setSourceDatabasePassphrase('');
    setRequiresSourceDatabasePassphrase(false);
    if (!details) {
      setSelectedPath('');
      setPreview(null);
      setApplyResult(null);
      setOverrides({});
      setStage('error');
      setError('Unable to resolve local file path from the dropped file. Click to browse and select the DB directly.');
      return;
    }

    setSelectedPath(details.path);
    void runPreview(details.path, '', '');
  }, [runPreview]);

  const handleBrowseRequested = useCallback(async (): Promise<void> => {
    setError(null);
    const sourcePath = await service.pickSqlImportSourcePathRequest();
    if (!sourcePath) {
      return;
    }
    setSelectedFileName(fileNameFromPath(sourcePath));
    setSelectedPath(sourcePath);
    setSourceEncryptionKey('');
    setSourceDatabasePassphrase('');
    setRequiresSourceDatabasePassphrase(false);
    void runPreview(sourcePath, '', '');
  }, [runPreview, service]);

  const setRowOverride = useCallback((sourceRowId: string, action: SqlImportAction) => {
    setOverrides((previous) => {
      if (!preview) {
        return previous;
      }
      const row = preview.rows.find((candidate) => candidate.sourceRowId === sourceRowId);
      if (!row) {
        return previous;
      }
      if (row.action === action) {
        const next = { ...previous };
        delete next[sourceRowId];
        return next;
      }
      return {
        ...previous,
        [sourceRowId]: action,
      };
    });
  }, [preview]);

  const importableRows = useMemo(() => {
    if (!preview) {
      return 0;
    }
    return preview.rows.reduce((total, row) => {
      const effectiveAction = overrides[row.sourceRowId] ?? row.action;
      return effectiveAction === 'new' || effectiveAction === 'update' ? total + 1 : total;
    }, 0);
  }, [overrides, preview]);

  const rescan = useCallback(async (): Promise<void> => {
    if (!selectedPath) {
      return;
    }
    await runPreview(selectedPath, sourceEncryptionKey, sourceDatabasePassphrase);
  }, [runPreview, selectedPath, sourceDatabasePassphrase, sourceEncryptionKey]);

  const applyImport = useCallback(async (): Promise<void> => {
    if (!selectedPath || !preview) {
      return;
    }
    setStage('importing');
    setError(null);
    try {
      const overrideEntries = Object.entries(overrides).map(([sourceRowId, action]) => ({
        sourceRowId,
        action,
      }));
      const response = await service.applySqlImportRequest({
        sourcePath: selectedPath,
        sourceEncryptionKey: sourceEncryptionKey.trim().length > 0 ? sourceEncryptionKey.trim() : undefined,
        sourceDatabasePassphrase:
          sourceDatabasePassphrase.trim().length > 0 ? sourceDatabasePassphrase.trim() : undefined,
        importWithoutOverwrite,
        overrides: overrideEntries,
      });
      setApplyResult(response);
      setStage('done');
    } catch (applyError) {
      setApplyResult(null);
      setStage('error');
      setError(getErrorMessage(applyError));
    }
  }, [importWithoutOverwrite, overrides, preview, selectedPath, service, sourceDatabasePassphrase, sourceEncryptionKey]);

  const reset = useCallback(() => {
    setSelectedFileName('');
    setSelectedPath('');
    setSourceEncryptionKey('');
    setSourceDatabasePassphrase('');
    setRequiresSourceDatabasePassphrase(false);
    setPreview(null);
    setApplyResult(null);
    setError(null);
    setOverrides({});
    setStage('idle');
  }, []);

  return {
    stage,
    selectedFileName,
    selectedPath,
    sourceEncryptionKey,
    sourceDatabasePassphrase,
    requiresSourceEncryptionKey: preview?.requiresSourceEncryptionKey === true,
    requiresSourceDatabasePassphrase,
    preview,
    applyResult,
    error,
    overrides,
    importableRows,
    handleFileSelected,
    handleBrowseRequested,
    setSourceEncryptionKey,
    setSourceDatabasePassphrase,
    setRowOverride,
    rescan,
    applyImport,
    reset,
  };
}
