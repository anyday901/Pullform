import type { EditorState } from '../editor/Editor';
import { formatLength } from '../core/units';

function hint(state: EditorState): string {
  const p = state.preview;
  switch (state.tool) {
    case 'select':
      return 'Select: click an object or face. Click empty space to clear. Delete removes the selected object.';
    case 'rectangle':
      return p?.kind === 'rectangle'
        ? 'Rectangle: click the opposite corner, or type width, depth and press Enter. Esc cancels.'
        : 'Rectangle: click the first corner on the ground (or drag), or type exact width and depth.';
    case 'pushpull':
      return p?.kind === 'pushpull'
        ? 'Push/Pull: move up and click to set the height, or type a distance and press Enter. Esc cancels.'
        : 'Push/Pull: click a rectangle or an extrusion cap, then move the pointer up.';
    case 'orbit':
      return 'Orbit: left-drag to orbit, right-drag to pan, wheel to zoom. Press Space to return to Select.';
  }
}

export function StatusBar({ state }: { state: EditorState }) {
  return (
    <div className="status" role="status" aria-live="polite">
      <span className="hint" data-testid="status-hint">{hint(state)}</span>
      {state.message && (
        <span className={`message ${state.message.kind}`} data-testid="status-message">
          {state.message.text}
        </span>
      )}
      <span className="snap-state">Snap: {state.snap.enabled ? formatLength(state.snap.step, state.doc.displayUnit) : 'off'}</span>
    </div>
  );
}
