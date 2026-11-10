import { formatLength } from '../core/units';
import type { EditorState } from '../editor/Editor';
import { faceLabel } from '../editor/selection';
import { rectangleSize } from '../geometry/rectangle';
import { findObject } from '../model/types';
import type { Actions } from './actions';
import { Icons } from './icons';
import { LengthField } from './LengthField';
import { BINDINGS, MOUSE_BINDINGS } from './platform';

export function PropertiesPanel({ state, actions }: { state: EditorState; actions: Actions }) {
  const unit = state.doc.displayUnit;
  const fmt = (m: number) => formatLength(m, unit);
  const obj = state.selection ? findObject(state.doc, state.selection.objectId) : undefined;
  const size = obj ? rectangleSize(obj.profile.boundary) : null;

  return (
    <aside className="properties" aria-label="Properties">
      <section>
        <h2>Selection</h2>
        {!obj || !size?.ok ? (
          <p className="muted">
            Nothing selected. {state.doc.objects.length === 0 ? 'Press R to draw a rectangle.' : `${state.doc.objects.length} object(s) in the model.`}
          </p>
        ) : (
          <div data-testid="selection-properties">
            <dl className="props">
              <dt>Name</dt>
              <dd data-testid="prop-name">{obj.name}</dd>
              <dt>ID</dt>
              <dd><code data-testid="prop-id">{obj.id}</code></dd>
              <dt>Type</dt>
              <dd>{obj.extrusion ? 'Extruded rectangle (solid)' : 'Rectangle (face)'}</dd>
              <dt>Face</dt>
              <dd data-testid="prop-face">{faceLabel(state.selection!.faceId)}</dd>
              <dt>Position</dt>
              <dd>{obj.transform.translation.map((v) => formatLength(v, unit, false)).join(', ')} {unit}</dd>
              <dt>Width (X)</dt>
              <dd data-testid="prop-width">{fmt(size.value.width)}</dd>
              <dt>Depth (Y)</dt>
              <dd data-testid="prop-depth">{fmt(size.value.depth)}</dd>
            </dl>
            <LengthField
              key={`${obj.id}-height`}
              label={obj.extrusion ? 'Total height (Z)' : 'Extrude to height'}
              testId="prop-height"
              hint="Total extrusion height measured from the base. Press Enter to apply."
              value={obj.extrusion ? formatLength(obj.extrusion.height, unit, false) : ''}
              onCommit={(text) => actions.tools.setTotalHeight(obj.id, text)}
            />
            <p className="muted small">
              Total height is absolute. The Push/Pull distance box is relative to the face's current position.
            </p>
            <button type="button" className="danger" onClick={() => actions.deleteSelection()} aria-label={`Delete ${obj.name}`}>
              {Icons.trash}<span>Delete object</span>
            </button>
          </div>
        )}
      </section>
      <section>
        <h2>Current limits</h2>
        <ul className="limits small">
          <li>Shapes: rectangles on the ground plane and their extrusions.</li>
          <li>Push/Pull: a rectangle, or an extrusion's top cap. Side and base faces can't be pushed yet.</li>
          <li>Heights must be positive; no holes, cutting or merging.</li>
          <li>Objects stay independent when they touch or overlap.</li>
        </ul>
      </section>
      <section>
        <h2>Controls</h2>
        <table className="bindings small">
          <tbody>
            {[...MOUSE_BINDINGS, ...BINDINGS].map((b) => (
              <tr key={b.action}>
                <td>{b.action}</td>
                <td><kbd>{b.keys}</kbd></td>
              </tr>
            ))}
          </tbody>
        </table>
      </section>
    </aside>
  );
}
