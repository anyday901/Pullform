import type { EditorState, ToolId } from '../editor/Editor';
import { formatLength } from '../core/units';
import type { Actions } from './actions';
import { Icons } from './icons';
import { keyFor } from './platform';

const TOOLS: { id: ToolId; label: string; action: string; hint: string }[] = [
  { id: 'select', label: 'Select', action: 'Select tool', hint: 'Select objects and faces' },
  { id: 'rectangle', label: 'Rectangle', action: 'Rectangle tool', hint: 'Draw a rectangle on the ground plane' },
  { id: 'pushpull', label: 'Push/Pull', action: 'Push/Pull tool', hint: 'Extrude a rectangle or move an extrusion cap' },
  { id: 'orbit', label: 'Orbit', action: 'Orbit tool', hint: 'Left-drag to orbit the camera' },
];

export function Toolbar({ state, actions }: { state: EditorState; actions: Actions }) {
  return (
    <aside className="toolbar" role="toolbar" aria-label="Tools" aria-orientation="vertical">
      {TOOLS.map((t) => (
        <button
          key={t.id}
          type="button"
          className={state.tool === t.id ? 'tool active' : 'tool'}
          aria-pressed={state.tool === t.id}
          aria-label={`${t.label} (${keyFor(t.action)})`}
          title={`${t.label} (${keyFor(t.action)}): ${t.hint}`}
          data-tool-id={t.id}
          onClick={() => actions.setTool(t.id)}
        >
          {Icons[t.id]}
          <span className="tool-label">{t.label}</span>
          <kbd>{keyFor(t.action)}</kbd>
        </button>
      ))}
      <hr />
      <button type="button" className="tool" onClick={() => actions.fit()} aria-label="Fit view to model (F)" title="Fit view to model (F)">
        {Icons.fit}
        <span className="tool-label">Fit</span>
        <kbd>F</kbd>
      </button>
      <button
        type="button"
        className={state.snap.enabled ? 'tool active' : 'tool'}
        aria-pressed={state.snap.enabled}
        onClick={() => actions.toggleSnap()}
        aria-label={`Grid snap ${state.snap.enabled ? 'on' : 'off'} (G)`}
        title={`Grid snap to ${formatLength(state.snap.step, state.doc.displayUnit)}: ${state.snap.enabled ? 'on' : 'off'} (G)`}
      >
        {Icons.snap}
        <span className="tool-label">Snap {state.snap.enabled ? 'on' : 'off'}</span>
        <kbd>G</kbd>
      </button>
      <select
        className="snap-step"
        aria-label="Grid snap increment"
        title="Grid snap increment"
        value={state.snap.step}
        onChange={(e) => actions.editor.setSnap({ step: Number(e.target.value) })}
      >
        {[0.001, 0.01, 0.05, 0.1, 0.5, 1].map((s) => (
          <option key={s} value={s}>{formatLength(s, state.doc.displayUnit)}</option>
        ))}
      </select>
    </aside>
  );
}
