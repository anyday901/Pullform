import type { Editor } from './editor/Editor';
import type { ToolController } from './tools/ToolController';
import type { Actions } from './ui/actions';
import type { Viewport } from './viewport/Viewport';

declare global {
  interface Window {
    /** Debug/test hook used by the browser integration tests. Not a public API. */
    __modeler?: { editor: Editor; tools: ToolController; actions: Actions; viewport: Viewport | null };
  }
}

export {};
