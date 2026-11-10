export const isMac = typeof navigator !== 'undefined' && /Mac|iPhone|iPad/i.test(navigator.platform || navigator.userAgent);
export const MOD = isMac ? '⌘' : 'Ctrl';

export interface Binding {
  action: string;
  keys: string;
}

/** The actual key bindings, shown in the interface. */
export const BINDINGS: Binding[] = [
  { action: 'Select tool', keys: 'Space' },
  { action: 'Rectangle tool', keys: 'R' },
  { action: 'Push/Pull tool', keys: 'P' },
  { action: 'Orbit tool', keys: 'O' },
  { action: 'Fit view to model', keys: 'F' },
  { action: 'Toggle grid snap', keys: 'G' },
  { action: 'Cancel / clear selection', keys: 'Esc' },
  { action: 'Delete selected object', keys: 'Delete / Backspace' },
  { action: 'Undo', keys: `${MOD}+Z` },
  { action: 'Redo', keys: isMac ? '⌘+Shift+Z' : 'Ctrl+Y / Ctrl+Shift+Z' },
  { action: 'Open project', keys: `${MOD}+O` },
  { action: 'Save project', keys: `${MOD}+S` },
  { action: 'Type a dimension', keys: 'digits, then Enter' },
];

export const MOUSE_BINDINGS: Binding[] = [
  { action: 'Use active tool', keys: 'Left button' },
  { action: 'Orbit', keys: 'Middle drag (or Orbit tool + left drag)' },
  { action: 'Pan', keys: 'Right drag or Shift+middle drag' },
  { action: 'Zoom', keys: 'Wheel (toward cursor)' },
];

export function keyFor(action: string): string {
  return BINDINGS.find((b) => b.action === action)?.keys ?? '';
}

/** True when keyboard input should go to a form control instead of shortcuts. */
export function isTypingTarget(target: EventTarget | null): boolean {
  if (!(target instanceof HTMLElement)) return false;
  if (target.isContentEditable) return true;
  const tag = target.tagName;
  if (tag === 'TEXTAREA' || tag === 'SELECT') return true;
  if (tag === 'INPUT') {
    const type = (target as HTMLInputElement).type;
    return !['button', 'checkbox', 'radio', 'submit', 'reset', 'file'].includes(type);
  }
  return false;
}
