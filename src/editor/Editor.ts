import type { LengthUnit } from '../core/units';
import type { Vec3 } from '../core/vec';
import type { RectanglePlacement } from '../geometry/rectangle';
import { History } from '../history/history';
import type { OpResult } from '../model/operations';
import { createEmptyDocument, type FaceId, type ModelDocument } from '../model/types';
import { sanitizeSelection, type Selection } from './selection';

export type ToolId = 'select' | 'rectangle' | 'pushpull' | 'orbit';

/** Transient previews. They are rendered but never stored in the document or history. */
export type Preview =
  | { kind: 'rectangle'; start: Vec3; end: Vec3; placement: RectanglePlacement | null }
  | {
      kind: 'pushpull';
      objectId: string;
      face: 'profile' | 'cap';
      /** Height before this gesture (0 for a planar profile). */
      startHeight: number;
      /** Incremental distance of this gesture. */
      delta: number;
      /** Resulting total height. */
      height: number;
      error: string | null;
    };

export interface Hover {
  objectId: string;
  faceId: FaceId;
}

export interface Message {
  kind: 'info' | 'error';
  text: string;
}

export interface SnapSettings {
  enabled: boolean;
  /** Grid snap increment in meters. */
  step: number;
}

export interface EditorState {
  doc: ModelDocument;
  selection: Selection;
  hover: Hover | null;
  tool: ToolId;
  preview: Preview | null;
  snap: SnapSettings;
  message: Message | null;
  fileName: string;
  /** The document as last saved/opened; the project is dirty when doc differs. */
  savedDoc: ModelDocument;
  undoLabel: string | null;
  redoLabel: string | null;
  /** Bumped when the viewport should refit the camera (e.g. after opening a file). */
  fitRequest: number;
}

export const DEFAULT_FILE_NAME = 'untitled.pullform.json';

type Listener = () => void;

/**
 * Framework-independent editor store. All committed model edits go through
 * `commit`, which validates, records exactly one undo entry, and clears redo.
 */
export class Editor {
  private state: EditorState;
  private readonly history: History;
  private readonly listeners = new Set<Listener>();

  constructor(doc: ModelDocument = createEmptyDocument(), historyLimit?: number) {
    this.history = new History(historyLimit);
    this.state = {
      doc,
      selection: null,
      hover: null,
      tool: 'select',
      preview: null,
      snap: { enabled: true, step: 0.1 },
      message: null,
      fileName: DEFAULT_FILE_NAME,
      savedDoc: doc,
      undoLabel: null,
      redoLabel: null,
      fitRequest: 0,
    };
  }

  getState = (): EditorState => this.state;

  subscribe = (listener: Listener): (() => void) => {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  };

  get doc(): ModelDocument {
    return this.state.doc;
  }

  get isDirty(): boolean {
    // Compared by content identity rather than document identity, because the display
    // unit can change without a model edit.
    const { doc, savedDoc } = this.state;
    return doc.objects !== savedDoc.objects || doc.displayUnit !== savedDoc.displayUnit;
  }

  get undoDepth(): number {
    return this.history.undoDepth;
  }
  get redoDepth(): number {
    return this.history.redoDepth;
  }

  private set(patch: Partial<EditorState>): void {
    this.state = {
      ...this.state,
      ...patch,
      undoLabel: this.history.undoLabel,
      redoLabel: this.history.redoLabel,
    };
    for (const l of this.listeners) l();
  }

  /**
   * The single path for committed model changes. On failure the document and
   * history are untouched and the error is shown.
   */
  commit(label: string, op: (doc: ModelDocument) => OpResult, selectAfter?: (r: OpResult & { ok: true }) => Selection): boolean {
    const result = op(this.state.doc);
    if (!result.ok) {
      this.set({ message: { kind: 'error', text: result.error } });
      return false;
    }
    if (result.doc === this.state.doc) return true;
    this.history.record({ label, doc: this.state.doc, selection: this.state.selection });
    const selection = sanitizeSelection(result.doc, selectAfter ? selectAfter(result) : this.state.selection);
    this.set({ doc: result.doc, selection, preview: null, hover: null, message: { kind: 'info', text: label } });
    return true;
  }

  /** A history snapshot with the current display unit, which is not part of undo history. */
  private restore(snapshot: ModelDocument): ModelDocument {
    const unit = this.state.doc.displayUnit;
    return snapshot.displayUnit === unit ? snapshot : { ...snapshot, displayUnit: unit };
  }

  undo(): boolean {
    this.cancelPreview();
    const entry = this.history.undo({ label: '', doc: this.state.doc, selection: this.state.selection });
    if (!entry) {
      this.set({ message: { kind: 'info', text: 'Nothing to undo.' } });
      return false;
    }
    const label = this.history.redoLabel;
    const doc = this.restore(entry.doc);
    this.set({ doc, selection: sanitizeSelection(doc, entry.selection), hover: null, message: { kind: 'info', text: `Undo: ${label}` } });
    return true;
  }

  redo(): boolean {
    this.cancelPreview();
    const entry = this.history.redo({ label: '', doc: this.state.doc, selection: this.state.selection });
    if (!entry) {
      this.set({ message: { kind: 'info', text: 'Nothing to redo.' } });
      return false;
    }
    const label = this.history.undoLabel;
    const doc = this.restore(entry.doc);
    this.set({ doc, selection: sanitizeSelection(doc, entry.selection), hover: null, message: { kind: 'info', text: `Redo: ${label}` } });
    return true;
  }

  setTool(tool: ToolId): void {
    this.set({ tool, preview: null, hover: null, message: null });
  }

  setPreview(preview: Preview | null): void {
    this.set({ preview });
  }

  cancelPreview(): boolean {
    if (!this.state.preview) return false;
    this.set({ preview: null });
    return true;
  }

  select(selection: Selection): void {
    this.set({ selection: sanitizeSelection(this.state.doc, selection) });
  }

  setHover(hover: Hover | null): void {
    const h = this.state.hover;
    if (h === hover || (h && hover && h.objectId === hover.objectId && h.faceId === hover.faceId)) return;
    this.set({ hover });
  }

  setSnap(snap: Partial<SnapSettings>): void {
    this.set({ snap: { ...this.state.snap, ...snap } });
  }

  setDisplayUnit(unit: LengthUnit): void {
    // Display unit is a view preference stored with the project, not an undoable edit:
    // undo/redo keep the current unit (see `restore`).
    const doc = { ...this.state.doc, displayUnit: unit };
    const clean = !this.isDirty;
    this.set({ doc, savedDoc: clean ? doc : this.state.savedDoc });
  }

  notify(message: Message | null): void {
    this.set({ message });
  }

  /** Replaces the project. History is cleared: undo never crosses a New/Open boundary. */
  loadDocument(doc: ModelDocument, fileName = DEFAULT_FILE_NAME): void {
    this.history.clear();
    this.set({
      doc,
      savedDoc: doc,
      fileName,
      selection: null,
      hover: null,
      preview: null,
      fitRequest: this.state.fitRequest + 1,
    });
  }

  newDocument(): void {
    this.loadDocument(createEmptyDocument(this.state.doc.displayUnit));
    this.notify({ kind: 'info', text: 'New project.' });
  }

  markSaved(fileName: string): void {
    this.set({ savedDoc: this.state.doc, fileName });
  }
}
