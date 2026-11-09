import type { Actions } from './actions';
import { isTypingTarget } from './platform';

/**
 * Global keyboard shortcuts. Ignored while typing in text fields, so dimensions
 * like "2000" can be entered normally. Returns a cleanup function.
 */
export function installShortcuts(actions: Actions, focusMeasurement: (initial: string) => boolean): () => void {
  const onKeyDown = (e: KeyboardEvent) => {
    if (e.defaultPrevented || isTypingTarget(e.target)) return;
    const mod = e.ctrlKey || e.metaKey;
    const key = e.key.toLowerCase();
    if (mod) {
      if (key === 'z' && !e.shiftKey) actions.undo();
      else if ((key === 'z' && e.shiftKey) || key === 'y') actions.redo();
      else if (key === 's') actions.saveProject();
      else if (key === 'o') actions.openProject();
      else return;
      e.preventDefault();
      return;
    }
    if (e.altKey) return;
    const onButton = e.target instanceof HTMLButtonElement || e.target instanceof HTMLSelectElement;
    switch (key) {
      case ' ':
        if (onButton) return; // let Space activate the focused button
        actions.setTool('select');
        break;
      case 'r':
        actions.setTool('rectangle');
        break;
      case 'p':
        actions.setTool('pushpull');
        break;
      case 'o':
        actions.setTool('orbit');
        break;
      case 'f':
        actions.fit();
        break;
      case 'g':
        actions.toggleSnap();
        break;
      case 'escape':
        actions.escape();
        break;
      case 'delete':
      case 'backspace':
        actions.deleteSelection();
        break;
      default:
        // Typing a number while drawing goes straight to the measurements box.
        if (/^[0-9.]$/.test(e.key) || (e.key === '-' && actions.editor.getState().tool === 'pushpull')) {
          if (focusMeasurement(e.key)) break;
        }
        return;
    }
    e.preventDefault();
  };
  const onBeforeUnload = (e: BeforeUnloadEvent) => {
    if (actions.editor.isDirty) {
      e.preventDefault();
      e.returnValue = '';
    }
  };
  window.addEventListener('keydown', onKeyDown);
  window.addEventListener('beforeunload', onBeforeUnload);
  return () => {
    window.removeEventListener('keydown', onKeyDown);
    window.removeEventListener('beforeunload', onBeforeUnload);
  };
}
