import { isFiniteNumber } from '../core/tolerance';
import type { Vec3 } from '../core/vec';
import type { ModelDocument } from '../model/types';
import { validateDocument } from '../model/validate';

/**
 * Native project file: application-owned, versioned JSON that stores the
 * editable model (not meshes). See docs/ARCHITECTURE.md for the schema.
 */
export const PROJECT_FORMAT = 'pullform-project';
export const PROJECT_VERSION = 1;
export const PROJECT_EXTENSION = '.pullform.json';
export const MAX_PROJECT_BYTES = 50 * 1024 * 1024;

export interface CameraView {
  position: Vec3;
  target: Vec3;
}

export interface ProjectFile {
  format: typeof PROJECT_FORMAT;
  version: typeof PROJECT_VERSION;
  generator: string;
  document: ModelDocument;
  /** Optional camera state; separate from model data and not part of undo history. */
  view?: CameraView;
}

export function serializeProject(doc: ModelDocument, view?: CameraView): string {
  const file: ProjectFile = {
    format: PROJECT_FORMAT,
    version: PROJECT_VERSION,
    generator: `Pullform ${__APP_VERSION__}`,
    document: doc,
    ...(view ? { view } : {}),
  };
  return JSON.stringify(file, null, 2) + '\n';
}

export type ParsedProject =
  | { ok: true; doc: ModelDocument; view: CameraView | null }
  | { ok: false; error: string };

function parseView(v: unknown): CameraView | null {
  // Camera state is optional and non-essential: an invalid view is ignored rather than rejecting the file.
  if (typeof v !== 'object' || v === null) return null;
  const r = v as Record<string, unknown>;
  const vec = (x: unknown): x is Vec3 => Array.isArray(x) && x.length === 3 && x.every(isFiniteNumber);
  if (!vec(r.position) || !vec(r.target)) return null;
  return { position: [...r.position], target: [...r.target] };
}

/**
 * Parses and fully validates a project file. Nothing is applied on failure, so the
 * caller can keep the current project intact.
 */
export function parseProject(text: string): ParsedProject {
  if (text.length > MAX_PROJECT_BYTES) return { ok: false, error: 'File is too large to be a project file.' };
  let data: unknown;
  try {
    data = JSON.parse(text);
  } catch {
    return { ok: false, error: 'The file is not valid JSON, so it cannot be a Pullform project.' };
  }
  if (typeof data !== 'object' || data === null || Array.isArray(data)) {
    return { ok: false, error: 'The file is not a Pullform project.' };
  }
  const file = data as Record<string, unknown>;
  if (file.format !== PROJECT_FORMAT) {
    return { ok: false, error: `The file is not a Pullform project (expected format "${PROJECT_FORMAT}").` };
  }
  if (!Number.isInteger(file.version)) return { ok: false, error: 'The project file has no valid version number.' };
  if (file.version !== PROJECT_VERSION) {
    return {
      ok: false,
      error: (file.version as number) > PROJECT_VERSION
        ? `This project uses file version ${file.version}, which is newer than this app supports (version ${PROJECT_VERSION}). Update Pullform to open it.`
        : `Unsupported project file version ${file.version}.`,
    };
  }
  const doc = validateDocument(file.document);
  if (!doc.ok) return { ok: false, error: `The project file is invalid: ${doc.error}` };
  return { ok: true, doc: doc.value, view: parseView(file.view) };
}

/** Ensures a download name ends with the project extension. */
export function projectFileName(name: string): string {
  const base = name.replace(/\.pullform\.json$/i, '').replace(/\.json$/i, '').replace(/[\\/:*?"<>|]+/g, '_').trim() || 'untitled';
  return base + PROJECT_EXTENSION;
}
