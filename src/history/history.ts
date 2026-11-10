import type { ModelDocument } from '../model/types';
import type { Selection } from '../editor/selection';

/**
 * Snapshot history. Documents are immutable and operations share unchanged
 * objects, so a snapshot costs one array plus the objects that changed.
 * Memory bound: at most `limit` undo entries (oldest are dropped) and the redo
 * branch, which is cleared by any new commit.
 */
export interface HistoryEntry {
  label: string;
  doc: ModelDocument;
  selection: Selection;
}

export const DEFAULT_HISTORY_LIMIT = 200;

export class History {
  private past: HistoryEntry[] = [];
  private future: HistoryEntry[] = [];
  constructor(readonly limit = DEFAULT_HISTORY_LIMIT) {}

  /** Records the state before a commit. Clears the redo branch. */
  record(before: HistoryEntry): void {
    this.past.push(before);
    if (this.past.length > this.limit) this.past.splice(0, this.past.length - this.limit);
    this.future = [];
  }

  undo(current: HistoryEntry): HistoryEntry | null {
    const prev = this.past.pop();
    if (!prev) return null;
    // The redo entry keeps the label of the operation being undone.
    this.future.push({ ...current, label: prev.label });
    return prev;
  }

  redo(current: HistoryEntry): HistoryEntry | null {
    const next = this.future.pop();
    if (!next) return null;
    this.past.push({ ...current, label: next.label });
    return next;
  }

  clear(): void {
    this.past = [];
    this.future = [];
  }

  get undoLabel(): string | null {
    return this.past.at(-1)?.label ?? null;
  }
  get redoLabel(): string | null {
    return this.future.at(-1)?.label ?? null;
  }
  get undoDepth(): number {
    return this.past.length;
  }
  get redoDepth(): number {
    return this.future.length;
  }
}
