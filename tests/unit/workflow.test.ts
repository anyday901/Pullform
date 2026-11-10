import { describe, expect, it } from 'vitest';
import { objectBounds } from '../../src/geometry/solid';
import { deleteObject } from '../../src/model/operations';
import { findObject } from '../../src/model/types';
import { parseProject, serializeProject } from '../../src/persistence/projectFile';
import { downRay, input, setup, sideViewRay } from './helpers';

/** Draw 2000 x 1000 mm by mouse (click, click) starting at (x0, y0). */
function drawByMouse(ctx: ReturnType<typeof setup>, x0 = 0, y0 = 0, w = 2, d = 1) {
  ctx.editor.setTool('rectangle');
  ctx.tools.pointerDown(input(downRay(x0, y0), [100, 100]));
  ctx.tools.pointerUp(input(downRay(x0, y0), [100, 100]));
  ctx.tools.pointerMove(input(downRay(x0 + w / 2, y0 + d / 2), [150, 120]));
  ctx.tools.pointerMove(input(downRay(x0 + w, y0 + d), [200, 140]));
  ctx.tools.pointerDown(input(downRay(x0 + w, y0 + d), [200, 140]));
  ctx.tools.pointerUp(input(downRay(x0 + w, y0 + d), [200, 140]));
  return ctx.editor.getState().selection!.objectId;
}

describe('rectangle tool', () => {
  it('draws by click-click with one undo entry and no history during preview', () => {
    const ctx = setup();
    ctx.editor.setTool('rectangle');
    ctx.tools.pointerDown(input(downRay(0, 0), [10, 10]));
    ctx.tools.pointerUp(input(downRay(0, 0), [10, 10]));
    for (let i = 1; i <= 20; i++) ctx.tools.pointerMove(input(downRay(i * 0.1, i * 0.05), [10 + i, 10 + i]));
    expect(ctx.editor.undoDepth).toBe(0);
    expect(ctx.editor.doc.objects.length).toBe(0);
    expect(ctx.editor.getState().preview?.kind).toBe('rectangle');
    ctx.tools.pointerDown(input(downRay(2, 1), [40, 40]));
    expect(ctx.editor.undoDepth).toBe(1);
    const obj = ctx.editor.doc.objects[0]!;
    const b = objectBounds(obj);
    expect(b.max[0] - b.min[0]).toBeCloseTo(2, 9);
    expect(b.max[1] - b.min[1]).toBeCloseTo(1, 9);
    expect(ctx.editor.getState().preview).toBeNull();
  });

  it('draws by press-drag-release in any direction', () => {
    const ctx = setup();
    ctx.editor.setTool('rectangle');
    ctx.tools.pointerDown(input(downRay(2, 1), [100, 100]));
    ctx.tools.pointerMove(input(downRay(0, 0), [10, 160]));
    ctx.tools.pointerUp(input(downRay(0, 0), [10, 160]));
    expect(ctx.editor.doc.objects.length).toBe(1);
    expect(ctx.editor.doc.objects[0]!.transform.translation).toEqual([0, 0, 0]);
    expect(ctx.editor.undoDepth).toBe(1);
  });

  it('creates exact rectangles through the same operation, following the pointer direction', () => {
    const ctx = setup();
    ctx.editor.setTool('rectangle');
    expect(ctx.tools.submitRectangleSize('2000', '1000')).toBeNull();
    let b = objectBounds(ctx.editor.doc.objects[0]!);
    expect(b.min).toEqual([0, 0, 0]);
    expect(b.max[0]).toBeCloseTo(2, 12);

    // With a first corner at (5,5) and the pointer toward -X/-Y.
    ctx.tools.pointerDown(input(downRay(5, 5), [0, 0]));
    ctx.tools.pointerUp(input(downRay(5, 5), [0, 0]));
    ctx.tools.pointerMove(input(downRay(4, 4), [3, 3]));
    expect(ctx.tools.submitRectangleSize('2m, 100cm')).toBeNull();
    b = objectBounds(ctx.editor.doc.objects[1]!);
    expect(b.min[0]).toBeCloseTo(3, 9);
    expect(b.min[1]).toBeCloseTo(4, 9);
    expect(b.max[0]).toBeCloseTo(5, 9);
    expect(ctx.editor.undoDepth).toBe(2);
  });

  it('rejects bad dimensions without touching the document or history', () => {
    const ctx = setup();
    ctx.editor.setTool('rectangle');
    for (const [w, d] of [['0', '1000'], ['abc', '1000'], ['1e999', '5'], ['-200', '100'], ['2000', '']]) {
      expect(ctx.tools.submitRectangleSize(w!, d), `${w},${d}`).not.toBeNull();
    }
    expect(ctx.editor.doc.objects.length).toBe(0);
    expect(ctx.editor.undoDepth).toBe(0);
  });

  it('cancels the preview on Escape without changing document or history', () => {
    const ctx = setup();
    drawByMouse(ctx);
    const docBefore = ctx.editor.doc;
    ctx.tools.pointerDown(input(downRay(3, 3), [0, 0]));
    ctx.tools.pointerUp(input(downRay(3, 3), [0, 0]));
    ctx.tools.pointerMove(input(downRay(5, 5), [50, 50]));
    expect(ctx.editor.getState().preview).not.toBeNull();
    expect(ctx.tools.cancel()).toBe(true);
    expect(ctx.editor.getState().preview).toBeNull();
    expect(ctx.editor.doc).toBe(docBefore);
    expect(ctx.editor.undoDepth).toBe(1);
    expect(ctx.editor.redoDepth).toBe(0);
    // The next click starts a fresh rectangle rather than finishing the cancelled one.
    ctx.tools.pointerDown(input(downRay(7, 7), [0, 0]));
    expect(ctx.editor.doc.objects.length).toBe(1);
  });

  it('snaps to the grid when enabled', () => {
    const ctx = setup();
    ctx.editor.setSnap({ enabled: true, step: 0.1 });
    ctx.editor.setTool('rectangle');
    ctx.tools.pointerDown(input(downRay(0.03, 0.04), [0, 0]));
    ctx.tools.pointerMove(input(downRay(1.98, 1.02), [90, 90]));
    ctx.tools.pointerUp(input(downRay(1.98, 1.02), [90, 90]));
    const b = objectBounds(ctx.editor.doc.objects[0]!);
    expect(b.min[0]).toBeCloseTo(0, 12);
    expect(b.max[0]).toBeCloseTo(2, 12);
    expect(b.max[1]).toBeCloseTo(1, 12);
  });
});

describe('push/pull workflow', () => {
  function pushFace(ctx: ReturnType<typeof setup>, id: string, faceId: 'profile' | 'cap', fromZ: number, toZ: number) {
    ctx.editor.setTool('pushpull');
    ctx.tools.pointerDown(input(sideViewRay(fromZ), [100, 100], { objectId: id, faceId, point: [1, 0.5, fromZ] }));
    ctx.tools.pointerUp(input(sideViewRay(fromZ), [100, 100]));
    ctx.tools.pointerMove(input(sideViewRay((fromZ + toZ) / 2), [100, 80]));
    ctx.tools.pointerMove(input(sideViewRay(toZ), [100, 60]));
    ctx.tools.pointerDown(input(sideViewRay(toZ), [100, 60]));
  }

  it('extrudes, edits the cap, undoes and redoes (2000 x 1000 x 750 → 900 mm)', () => {
    const ctx = setup();
    ctx.editor.setSnap({ enabled: false });
    const id = drawByMouse(ctx);
    expect(ctx.editor.undoDepth).toBe(1);

    pushFace(ctx, id, 'profile', 0, 0.75);
    const obj = findObject(ctx.editor.doc, id)!;
    expect(obj.extrusion!.height).toBeCloseTo(0.75, 9);
    expect(ctx.editor.undoDepth).toBe(2);
    expect(ctx.editor.getState().selection).toEqual({ objectId: id, faceId: 'cap' });

    // Exact total height from the properties panel.
    expect(ctx.tools.setTotalHeight(id, '900')).toBeNull();
    expect(ctx.editor.doc.objects.length).toBe(1);
    expect(findObject(ctx.editor.doc, id)!.extrusion!.height).toBeCloseTo(0.9, 12);
    expect(ctx.editor.getState().selection).toEqual({ objectId: id, faceId: 'cap' });

    // Cap remains selectable and push/pullable after the edit.
    pushFace(ctx, id, 'cap', 0.9, 1.0);
    expect(findObject(ctx.editor.doc, id)!.extrusion!.height).toBeCloseTo(1.0, 9);
    ctx.editor.undo();
    expect(findObject(ctx.editor.doc, id)!.extrusion!.height).toBeCloseTo(0.9, 12);

    ctx.editor.undo();
    expect(findObject(ctx.editor.doc, id)!.extrusion!.height).toBeCloseTo(0.75, 9);
    ctx.editor.undo();
    expect(findObject(ctx.editor.doc, id)!.extrusion).toBeNull();
    expect(ctx.editor.getState().selection).toEqual({ objectId: id, faceId: 'profile' });

    ctx.editor.redo();
    expect(findObject(ctx.editor.doc, id)!.extrusion!.height).toBeCloseTo(0.75, 9);
    ctx.editor.redo();
    expect(findObject(ctx.editor.doc, id)!.extrusion!.height).toBeCloseTo(0.9, 12);
    expect(ctx.editor.getState().selection?.faceId).toBe('cap');
  });

  it('uses incremental distance for typed push/pull on a cap and total height in properties', () => {
    const ctx = setup();
    ctx.editor.setTool('rectangle');
    ctx.tools.submitRectangleSize('2000', '1000');
    ctx.editor.setTool('pushpull');
    expect(ctx.tools.submitPushDistance('750mm')).toBeNull();
    const id = ctx.editor.doc.objects[0]!.id;
    expect(findObject(ctx.editor.doc, id)!.extrusion!.height).toBeCloseTo(0.75, 12);
    expect(ctx.tools.submitPushDistance('150')).toBeNull(); // +150 mm
    expect(findObject(ctx.editor.doc, id)!.extrusion!.height).toBeCloseTo(0.9, 12);
    expect(ctx.tools.submitPushDistance('-1000')).not.toBeNull(); // would cross the base
    expect(ctx.tools.setTotalHeight(id, '0')).not.toBeNull();
    expect(ctx.tools.setTotalHeight(id, 'tall')).not.toBeNull();
    expect(findObject(ctx.editor.doc, id)!.extrusion!.height).toBeCloseTo(0.9, 12);
    expect(ctx.editor.undoDepth).toBe(3);
  });

  it('cancels a push/pull preview without changing document or history', () => {
    const ctx = setup();
    const id = drawByMouse(ctx);
    const docBefore = ctx.editor.doc;
    ctx.editor.setTool('pushpull');
    ctx.tools.pointerDown(input(sideViewRay(0), [100, 100], { objectId: id, faceId: 'profile', point: [1, 0.5, 0] }));
    ctx.tools.pointerUp(input(sideViewRay(0), [100, 100]));
    ctx.tools.pointerMove(input(sideViewRay(0.5), [100, 50]));
    const preview = ctx.editor.getState().preview;
    expect(preview?.kind === 'pushpull' && preview.height).toBeCloseTo(0.5, 1);
    ctx.tools.cancel();
    expect(ctx.editor.getState().preview).toBeNull();
    expect(ctx.editor.doc).toBe(docBefore);
    expect(findObject(ctx.editor.doc, id)!.extrusion).toBeNull();
    expect(ctx.editor.undoDepth).toBe(1);
  });

  it('refuses negative push/pull and unsupported faces cleanly', () => {
    const ctx = setup();
    const id = drawByMouse(ctx);
    pushFace(ctx, id, 'profile', 0, -0.5);
    expect(findObject(ctx.editor.doc, id)!.extrusion).toBeNull();
    expect(ctx.editor.getState().message?.kind).toBe('error');
    ctx.tools.cancel();
    expect(ctx.editor.undoDepth).toBe(1);

    ctx.tools.submitPushDistance('500');
    ctx.tools.pointerDown(input(sideViewRay(0.2), [0, 0], { objectId: id, faceId: 'side-0', point: [1, 0, 0.2] }));
    expect(ctx.editor.getState().preview).toBeNull();
    expect(ctx.editor.getState().message?.text).toMatch(/side faces/);
    expect(ctx.editor.undoDepth).toBe(2);
  });
});

describe('selection, deletion and history', () => {
  it('deletes one of two independent objects and undo restores IDs and selection', () => {
    const ctx = setup();
    const a = drawByMouse(ctx, 0, 0);
    const b = drawByMouse(ctx, 1, 0.5); // overlapping, stays independent
    expect(a).not.toBe(b);
    expect(ctx.editor.doc.objects.map((o) => o.id)).toEqual([a, b]);

    ctx.editor.setTool('select');
    ctx.tools.pointerDown(input(downRay(2.5, 1.2), [0, 0], { objectId: b, faceId: 'profile', point: [2.5, 1.2, 0] }));
    expect(ctx.editor.getState().selection).toEqual({ objectId: b, faceId: 'profile' });
    ctx.editor.commit('Delete', (doc) => deleteObject(doc, b), () => null);
    expect(ctx.editor.doc.objects.map((o) => o.id)).toEqual([a]);
    expect(ctx.editor.getState().selection).toBeNull();

    ctx.editor.undo();
    expect(ctx.editor.doc.objects.map((o) => o.id)).toEqual([a, b]);
    expect(ctx.editor.getState().selection).toEqual({ objectId: b, faceId: 'profile' });
    ctx.editor.redo();
    expect(ctx.editor.doc.objects.map((o) => o.id)).toEqual([a]);
    expect(ctx.editor.getState().selection).toBeNull();

    // A new object never reuses a deleted ID.
    ctx.editor.undo();
    ctx.editor.redo();
    const c = drawByMouse(ctx, 5, 5);
    expect(c).not.toBe(b);
    expect(ctx.editor.redoDepth).toBe(0);
  });

  it('clicking empty space clears the selection', () => {
    const ctx = setup();
    drawByMouse(ctx);
    ctx.editor.setTool('select');
    ctx.tools.pointerDown(input(downRay(20, 20), [0, 0], null));
    expect(ctx.editor.getState().selection).toBeNull();
  });

  it('a new edit after undo clears the redo branch', () => {
    const ctx = setup();
    drawByMouse(ctx);
    ctx.editor.undo();
    expect(ctx.editor.redoDepth).toBe(1);
    drawByMouse(ctx, 3, 3);
    expect(ctx.editor.redoDepth).toBe(0);
    expect(ctx.editor.redo()).toBe(false);
  });

  it('keeps the chosen display unit across undo and redo', () => {
    const ctx = setup();
    const id = drawByMouse(ctx);
    ctx.editor.setDisplayUnit('in');
    ctx.editor.undo();
    expect(ctx.editor.doc.displayUnit).toBe('in');
    expect(ctx.editor.doc.objects).toHaveLength(0);
    ctx.editor.redo();
    expect(ctx.editor.doc.displayUnit).toBe('in');
    expect(ctx.editor.doc.objects[0]!.id).toBe(id);
    // Unitless input now means inches.
    expect(ctx.tools.setTotalHeight(id, '10')).toBeNull();
    expect(ctx.editor.doc.objects[0]!.extrusion!.height).toBeCloseTo(0.254, 12);
  });

  it('tracks unsaved changes by content, including the display unit', () => {
    const ctx = setup();
    expect(ctx.editor.isDirty).toBe(false);
    ctx.editor.setDisplayUnit('cm'); // nothing to lose yet: stays clean
    expect(ctx.editor.isDirty).toBe(false);
    drawByMouse(ctx);
    expect(ctx.editor.isDirty).toBe(true);
    ctx.editor.setDisplayUnit('m');
    ctx.editor.undo();
    // The model is back to the saved state, but the unit change is still unsaved.
    expect(ctx.editor.doc.objects).toHaveLength(0);
    expect(ctx.editor.isDirty).toBe(true);
    ctx.editor.setDisplayUnit('cm');
    expect(ctx.editor.isDirty).toBe(false);
  });

  it('bounds the undo history', async () => {
    const { Editor } = await import('../../src/editor/Editor');
    const { ToolController } = await import('../../src/tools/ToolController');
    const editor = new Editor(undefined, 3);
    const tools = new ToolController(editor);
    editor.setTool('rectangle');
    for (let i = 0; i < 5; i++) tools.submitRectangleSize('100', '100');
    expect(editor.undoDepth).toBe(3);
  });
});

describe('project files', () => {
  it('saves, reloads and continues editing with the same IDs, units and geometry', () => {
    const ctx = setup();
    ctx.editor.setTool('rectangle');
    ctx.tools.submitRectangleSize('2000', '1000');
    ctx.editor.setTool('pushpull');
    ctx.tools.submitPushDistance('750');
    ctx.editor.setTool('rectangle');
    ctx.tools.submitRectangleSize('500', '500');
    const text = serializeProject(ctx.editor.doc, { position: [5, -5, 5], target: [0, 0, 0] });

    const parsed = parseProject(text);
    expect(parsed.ok).toBe(true);
    if (!parsed.ok) return;
    expect(parsed.doc).toEqual(ctx.editor.doc);
    expect(parsed.view).toEqual({ position: [5, -5, 5], target: [0, 0, 0] });

    const reopened = setup();
    reopened.editor.loadDocument(parsed.doc, 'table.pullform.json');
    expect(reopened.editor.isDirty).toBe(false);
    expect(reopened.editor.undoDepth).toBe(0);
    const [first, second] = reopened.editor.doc.objects;
    expect([first!.id, second!.id]).toEqual(['obj-1', 'obj-2']);
    expect(reopened.tools.setTotalHeight(first!.id, '0.9m')).toBeNull();
    expect(findObject(reopened.editor.doc, 'obj-1')!.extrusion!.height).toBeCloseTo(0.9, 12);
    expect(reopened.editor.isDirty).toBe(true);
    reopened.editor.setTool('rectangle');
    reopened.tools.submitRectangleSize('100', '100');
    expect(reopened.editor.doc.objects.at(-1)!.id).toBe('obj-3');
    reopened.editor.undo();
    reopened.editor.undo();
    expect(reopened.editor.isDirty).toBe(false);
  });

  it('rejects malformed files and unsupported versions with clear errors', () => {
    const ctx = setup();
    ctx.editor.setTool('rectangle');
    ctx.tools.submitRectangleSize('2000', '1000');
    const good = JSON.parse(serializeProject(ctx.editor.doc));
    const variants: [string, unknown][] = [
      ['not json', '{oops'],
      ['wrong format', { ...good, format: 'something-else' }],
      ['future version', { ...good, version: 99 }],
      ['missing version', { ...good, version: undefined }],
      ['bad unit', { ...good, document: { ...good.document, lengthUnit: 'ft' } }],
      ['duplicate id', { ...good, document: { ...good.document, nextId: 5, objects: [good.document.objects[0], good.document.objects[0]] } }],
      ['nextId too low', { ...good, document: { ...good.document, nextId: 1 } }],
      ['degenerate', { ...good, document: { ...good.document, objects: [{ ...good.document.objects[0], profile: { ...good.document.objects[0].profile, boundary: [[0, 0], [0, 0], [0, 0], [0, 0]] } }] } }],
      ['non-finite (null)', { ...good, document: { ...good.document, objects: [{ ...good.document.objects[0], transform: { translation: [0, null, 0] } }] } }],
      ['negative height', { ...good, document: { ...good.document, objects: [{ ...good.document.objects[0], extrusion: { height: -1 } }] } }],
      ['clockwise', { ...good, document: { ...good.document, objects: [{ ...good.document.objects[0], profile: { ...good.document.objects[0].profile, boundary: [[0, 0], [0, 1], [2, 1], [2, 0]] } }] } }],
      ['bad id', { ...good, document: { ...good.document, objects: [{ ...good.document.objects[0], id: 'x' }] } }],
      ['array', []],
    ];
    for (const [name, v] of variants) {
      const text = typeof v === 'string' ? v : JSON.stringify(v);
      const r = parseProject(text);
      expect(r.ok, name).toBe(false);
      if (!r.ok) expect(r.error.length, name).toBeGreaterThan(10);
    }
    const future = parseProject(JSON.stringify({ ...good, version: 99 }));
    expect(!future.ok && future.error).toMatch(/newer/);
    // The open project is untouched by failed parses (nothing was applied).
    expect(ctx.editor.doc.objects.length).toBe(1);
  });
});
