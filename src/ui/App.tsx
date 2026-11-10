import { useEffect, useRef, useState } from 'react';
import { LENGTH_UNITS, type LengthUnit } from '../core/units';
import { Editor } from '../editor/Editor';
import { ToolController } from '../tools/ToolController';
import { Viewport } from '../viewport/Viewport';
import { Actions } from './actions';
import { Icons } from './icons';
import { MeasurementsBar, type MeasurementsHandle } from './MeasurementsBar';
import { keyFor, MOD } from './platform';
import { PropertiesPanel } from './PropertiesPanel';
import { installShortcuts } from './shortcuts';
import { StatusBar } from './StatusBar';
import { Toolbar } from './Toolbar';
import { useEditorState } from './useEditor';

function createApp() {
  const editor = new Editor();
  const tools = new ToolController(editor);
  const actions = new Actions(editor, tools);
  return { editor, tools, actions };
}

export function App() {
  const [{ editor, tools, actions }] = useState(createApp);
  const state = useEditorState(editor);
  const viewportHost = useRef<HTMLDivElement>(null);
  const fileInput = useRef<HTMLInputElement>(null);
  const measurements = useRef<MeasurementsHandle>(null);

  useEffect(() => {
    const viewport = new Viewport(viewportHost.current!, editor, tools);
    actions.viewport = viewport;
    window.__modeler = { editor, tools, actions, viewport };
    return () => {
      viewport.dispose();
      actions.viewport = null;
      delete window.__modeler;
    };
  }, [editor, tools, actions]);

  useEffect(() => {
    actions.pickFile = () => fileInput.current?.click();
    return installShortcuts(actions, (initial) => measurements.current?.focusWith(initial) ?? false);
  }, [actions]);

  useEffect(() => {
    const name = state.fileName.replace(/\.pullform\.json$/i, '');
    document.title = `${editor.isDirty ? '• ' : ''}${name} — Pullform`;
  }, [state.doc, state.savedDoc, state.fileName, editor]);

  return (
    <div className="app">
      <header className="topbar">
        <div className="brand" aria-label="Pullform">
          <img src="./favicon.svg" alt="" width={22} height={22} />
          <span>Pullform</span>
        </div>
        <nav className="file-actions" aria-label="File">
          <button type="button" onClick={() => actions.newProject()} title="New project" aria-label="New project">
            {Icons.newFile}<span>New</span>
          </button>
          <button type="button" onClick={() => actions.openProject()} title={`Open project (${MOD}+O)`} aria-label="Open project">
            {Icons.open}<span>Open…</span>
          </button>
          <button type="button" onClick={() => actions.saveProject()} title={`Save project as a download (${MOD}+S)`} aria-label="Save project">
            {Icons.save}<span>Save</span>
          </button>
          <input
            ref={fileInput}
            type="file"
            accept=".json,application/json"
            hidden
            data-testid="open-file-input"
            onChange={(e) => {
              const file = e.target.files?.[0];
              e.target.value = '';
              if (file) void actions.openFile(file);
            }}
          />
        </nav>
        <nav className="edit-actions" aria-label="Edit">
          <button type="button" onClick={() => actions.undo()} disabled={!state.undoLabel} title={state.undoLabel ? `Undo ${state.undoLabel} (${keyFor('Undo')})` : 'Nothing to undo'} aria-label="Undo">
            {Icons.undo}
          </button>
          <button type="button" onClick={() => actions.redo()} disabled={!state.redoLabel} title={state.redoLabel ? `Redo ${state.redoLabel} (${keyFor('Redo')})` : 'Nothing to redo'} aria-label="Redo">
            {Icons.redo}
          </button>
        </nav>
        <div className="doc-title" title={state.fileName}>
          {state.fileName}
          {editor.isDirty && <span className="dirty" title="Unsaved changes" aria-label="Unsaved changes"> •</span>}
        </div>
        <label className="unit-select">
          Units
          <select value={state.doc.displayUnit} onChange={(e) => editor.setDisplayUnit(e.target.value as LengthUnit)} aria-label="Display unit">
            {LENGTH_UNITS.map((u) => (
              <option key={u} value={u}>{u}</option>
            ))}
          </select>
        </label>
      </header>
      <Toolbar state={state} actions={actions} />
      <main className="viewport" ref={viewportHost} data-tool={state.tool} />
      <PropertiesPanel state={state} actions={actions} />
      <footer className="bottombar">
        <MeasurementsBar ref={measurements} state={state} tools={tools} />
        <StatusBar state={state} />
      </footer>
    </div>
  );
}
