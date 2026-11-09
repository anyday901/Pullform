import { deleteObject } from '../model/operations';
import { findObject } from '../model/types';
import { parseProject, projectFileName, serializeProject } from '../persistence/projectFile';
import type { Editor, ToolId } from '../editor/Editor';
import type { ToolController } from '../tools/ToolController';
import type { Viewport } from '../viewport/Viewport';

export const DISCARD_PROMPT = 'You have unsaved changes. Discard them?';

/** User-level commands shared by buttons and keyboard shortcuts. */
export class Actions {
  viewport: Viewport | null = null;
  /** Injected so tests and the UI can replace the native dialog. */
  confirmDiscard: () => boolean = () => window.confirm(DISCARD_PROMPT);
  /** Opens a file picker; set by the App component. */
  pickFile: () => void = () => {};

  constructor(readonly editor: Editor, readonly tools: ToolController) {}

  setTool(tool: ToolId): void {
    this.tools.cancel();
    this.editor.setTool(tool);
  }

  escape(): void {
    if (this.tools.cancel()) {
      this.editor.notify({ kind: 'info', text: 'Cancelled.' });
    } else if (this.editor.getState().selection) {
      this.editor.select(null);
    }
  }

  undo(): void {
    this.tools.cancel();
    this.editor.undo();
  }

  redo(): void {
    this.tools.cancel();
    this.editor.redo();
  }

  deleteSelection(): void {
    const sel = this.editor.getState().selection;
    if (!sel) {
      this.editor.notify({ kind: 'info', text: 'Select an object to delete.' });
      return;
    }
    const name = findObject(this.editor.doc, sel.objectId)?.name ?? sel.objectId;
    this.tools.cancel();
    this.editor.commit(`Delete ${name}`, (doc) => deleteObject(doc, sel.objectId), () => null);
  }

  fit(): void {
    this.viewport?.fitToModel();
  }

  toggleSnap(): void {
    const { enabled } = this.editor.getState().snap;
    this.editor.setSnap({ enabled: !enabled });
    this.editor.notify({ kind: 'info', text: `Grid snap ${enabled ? 'off' : 'on'}.` });
  }

  newProject(): void {
    if (this.editor.isDirty && !this.confirmDiscard()) return;
    this.tools.cancel();
    this.editor.newDocument();
  }

  openProject(): void {
    if (this.editor.isDirty && !this.confirmDiscard()) return;
    this.pickFile();
  }

  /** Validates the whole file before replacing anything; on failure the current project is untouched. */
  async openFile(file: File): Promise<void> {
    let text: string;
    try {
      text = await file.text();
    } catch {
      this.editor.notify({ kind: 'error', text: `Could not read ${file.name}.` });
      return;
    }
    const parsed = parseProject(text);
    if (!parsed.ok) {
      this.editor.notify({ kind: 'error', text: `Could not open ${file.name}: ${parsed.error}` });
      return;
    }
    this.tools.cancel();
    this.editor.loadDocument(parsed.doc, file.name);
    if (parsed.view) this.viewport?.setView(parsed.view);
    else this.viewport?.fitToModel();
    this.editor.notify({ kind: 'info', text: `Opened ${file.name} (${parsed.doc.objects.length} objects). Undo history starts fresh.` });
  }

  /** Download-based save; works in every browser, including Firefox. */
  saveProject(): void {
    this.tools.cancel();
    const name = projectFileName(this.editor.getState().fileName);
    const text = serializeProject(this.editor.doc, this.viewport?.getView());
    const url = URL.createObjectURL(new Blob([text], { type: 'application/json' }));
    const a = document.createElement('a');
    a.href = url;
    a.download = name;
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
    this.editor.markSaved(name);
    this.editor.notify({ kind: 'info', text: `Saved ${name} to your downloads.` });
  }
}
