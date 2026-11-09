import { forwardRef, useImperativeHandle, useRef } from 'react';
import { formatLength } from '../core/units';
import type { EditorState } from '../editor/Editor';
import { rectangleSize } from '../geometry/rectangle';
import { findObject } from '../model/types';
import type { ToolController } from '../tools/ToolController';
import { LengthField } from './LengthField';

export interface MeasurementsHandle {
  /** Focuses the first measurement field and starts it with `initial`. Returns false if no field applies. */
  focusWith(initial: string): boolean;
}

interface Props {
  state: EditorState;
  tools: ToolController;
}

export const MeasurementsBar = forwardRef<MeasurementsHandle, Props>(function MeasurementsBar({ state, tools }, ref) {
  const first = useRef<HTMLInputElement | null>(null);
  const second = useRef<HTMLInputElement | null>(null);
  const unit = state.doc.displayUnit;
  const f = (m: number) => formatLength(m, unit, false);

  useImperativeHandle(ref, () => ({
    focusWith(initial: string) {
      const input = first.current;
      if (!input || input.disabled) return false;
      input.focus();
      // Use the native setter so React sees the change.
      const setter = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')!.set!;
      setter.call(input, initial);
      input.dispatchEvent(new Event('input', { bubbles: true }));
      input.setSelectionRange(initial.length, initial.length);
      return true;
    },
  }));

  const preview = state.preview;
  let content: React.ReactNode;
  if (state.tool === 'rectangle') {
    const placement = preview?.kind === 'rectangle' ? preview.placement : null;
    const w = placement ? f(placement.width) : '';
    const d = placement ? f(placement.depth) : '';
    content = (
      <>
        <LengthField
          key="rect-w"
          transient
          label="Width"
          testId="measure-width"
          value={w}
          inputRef={(el) => (first.current = el)}
          hint={`Width along X. Unitless values are ${unit}. You can also type "width, depth" here.`}
          onCommit={(text) => {
            if (/[;,]/.test(text)) return tools.submitRectangleSize(text);
            const depth = second.current?.value ?? '';
            if (depth.trim() === '') {
              second.current?.focus();
              return null;
            }
            return tools.submitRectangleSize(text, depth);
          }}
        />
        <LengthField
          key="rect-d"
          transient
          label="Depth"
          testId="measure-depth"
          value={d}
          inputRef={(el) => (second.current = el)}
          hint={`Depth along Y. Unitless values are ${unit}.`}
          onCommit={(text) => {
            const width = (first.current?.value ?? '').trim();
            if (!width) return 'Enter a width first.';
            return tools.submitRectangleSize(width, text);
          }}
        />
        <span className="measure-note">{preview?.kind === 'rectangle' ? 'From the first corner, toward the pointer' : 'Starts at the origin unless a first corner is placed'}</span>
      </>
    );
  } else if (state.tool === 'pushpull') {
    const pp = preview?.kind === 'pushpull' ? preview : null;
    const sel = state.selection ? findObject(state.doc, state.selection.objectId) : undefined;
    const total = pp ? pp.height : sel?.extrusion?.height;
    content = (
      <>
        <LengthField
          key="push-d"
          transient
          label="Distance (Δ)"
          testId="measure-distance"
          value={pp ? f(pp.delta) : ''}
          inputRef={(el) => (first.current = el)}
          disabled={!pp && !sel}
          hint="Push/pull distance from the face's current position. Enter applies it."
          onCommit={(text) => tools.submitPushDistance(text)}
        />
        <span className="measure-readout" data-testid="measure-total">
          Total height: {total !== undefined ? formatLength(total, unit) : '—'}
          {pp?.error && <span className="field-error"> {pp.error}</span>}
        </span>
      </>
    );
  } else {
    const sel = state.selection ? findObject(state.doc, state.selection.objectId) : undefined;
    const size = sel ? rectangleSize(sel.profile.boundary) : null;
    content = (
      <span className="measure-readout" data-testid="measure-readout">
        {sel && size?.ok
          ? `${sel.name}: ${f(size.value.width)} × ${f(size.value.depth)}${sel.extrusion ? ` × ${f(sel.extrusion.height)}` : ''} ${unit}`
          : 'Measurements appear here while drawing.'}
      </span>
    );
  }
  return (
    <div className="measurements" role="group" aria-label="Measurements">
      <span className="measure-title">Measurements</span>
      {content}
    </div>
  );
});
