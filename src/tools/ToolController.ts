import { formatLength, parseLength, parseLengthList } from '../core/units';
import { closestParameterOnAxis, intersectRayWithHorizontalPlane, type Ray, type Vec3 } from '../core/vec';
import type { Editor } from '../editor/Editor';
import { faceLabel } from '../editor/selection';
import { rectangleFromCorners, rectangleFromSignedSize, type RectanglePlacement } from '../geometry/rectangle';
import { checkHeight, createRectangle, extrudeProfile, setExtrusionHeight } from '../model/operations';
import { findObject, type FaceId } from '../model/types';

/** What the viewport reports for each pointer event. */
export interface PointerInput {
  /** World-space pick ray through the cursor. */
  ray: Ray;
  /** Logical face under the cursor, if any. */
  pick: { objectId: string; faceId: FaceId; point: Vec3 } | null;
  /** Cursor position in CSS pixels, used to tell clicks from drags. */
  screen: [number, number];
}

const DRAG_THRESHOLD_PX = 5;

function snapValue(v: number, step: number): number {
  return Math.round(v / step) * step;
}

/**
 * Interaction state machines for the modeling tools. Pointer movement only
 * updates the editor's transient preview; a completed gesture commits exactly
 * one operation. Escape (`cancel`) discards the preview without touching the
 * document or history.
 */
export class ToolController {
  private rect: { start: Vec3; pressScreen: [number, number]; pressed: boolean } | null = null;
  private push: {
    objectId: string;
    face: 'profile' | 'cap';
    anchor: Vec3;
    startHeight: number;
    pressScreen: [number, number];
    pressed: boolean;
  } | null = null;

  constructor(private readonly editor: Editor) {}

  get active(): boolean {
    return this.rect !== null || this.push !== null;
  }

  private snapPoint(p: Vec3): Vec3 {
    const { enabled, step } = this.editor.getState().snap;
    if (!enabled) return p;
    return [snapValue(p[0], step), snapValue(p[1], step), p[2]];
  }

  private groundPoint(input: PointerInput): Vec3 | null {
    const p = intersectRayWithHorizontalPlane(input.ray, 0);
    return p ? this.snapPoint(p) : null;
  }

  /** Called when the tool changes or Escape is pressed. */
  cancel(): boolean {
    const had = this.active || this.editor.getState().preview !== null;
    this.rect = null;
    this.push = null;
    this.editor.cancelPreview();
    return had;
  }

  pointerDown(input: PointerInput): void {
    const tool = this.editor.getState().tool;
    if (tool === 'select') return this.selectDown(input);
    if (tool === 'rectangle') return this.rectDown(input);
    if (tool === 'pushpull') return this.pushDown(input);
  }

  pointerMove(input: PointerInput): void {
    const tool = this.editor.getState().tool;
    if (tool === 'select') {
      this.editor.setHover(input.pick ? { objectId: input.pick.objectId, faceId: input.pick.faceId } : null);
    } else if (tool === 'rectangle') {
      this.rectMove(input);
    } else if (tool === 'pushpull') {
      this.pushMove(input);
    }
  }

  pointerUp(input: PointerInput): void {
    const moved = (press: [number, number]) =>
      Math.hypot(input.screen[0] - press[0], input.screen[1] - press[1]) > DRAG_THRESHOLD_PX;
    if (this.rect?.pressed) {
      this.rect.pressed = false;
      // Press-drag-release draws in one gesture; a plain click waits for the second corner.
      if (moved(this.rect.pressScreen)) this.rectFinish(input);
    } else if (this.push?.pressed) {
      this.push.pressed = false;
      if (moved(this.push.pressScreen)) this.pushFinish();
    }
  }

  // ---- Select -------------------------------------------------------------

  private selectDown(input: PointerInput): void {
    this.editor.select(input.pick ? { objectId: input.pick.objectId, faceId: input.pick.faceId } : null);
  }

  // ---- Rectangle ----------------------------------------------------------

  private rectDown(input: PointerInput): void {
    const p = this.groundPoint(input);
    if (!p) return;
    if (!this.rect) {
      this.rect = { start: p, pressScreen: input.screen, pressed: true };
      this.editor.setPreview({ kind: 'rectangle', start: p, end: p, placement: null });
      this.editor.notify(null);
    } else {
      this.rectFinish(input);
    }
  }

  private rectMove(input: PointerInput): void {
    if (!this.rect) return;
    const p = this.groundPoint(input);
    if (!p) return;
    const r = rectangleFromCorners(this.rect.start, p);
    this.editor.setPreview({ kind: 'rectangle', start: this.rect.start, end: p, placement: r.ok ? r.value : null });
  }

  private rectFinish(input: PointerInput): void {
    if (!this.rect) return;
    const p = this.groundPoint(input);
    if (!p) return;
    const r = rectangleFromCorners(this.rect.start, p);
    if (!r.ok) {
      // Keep the first corner so the user can pick a valid second corner.
      this.editor.notify({ kind: 'error', text: r.error });
      return;
    }
    this.commitRectangle(r.value);
  }

  private commitRectangle(placement: RectanglePlacement): boolean {
    const ok = this.editor.commit('Draw rectangle', (doc) => createRectangle(doc, placement), (res) =>
      res.objectId ? { objectId: res.objectId, faceId: 'profile' } : null,
    );
    if (ok) {
      this.rect = null;
      this.editor.cancelPreview();
    }
    return ok;
  }

  /**
   * Exact rectangle entry. With a first corner placed, the typed size is drawn
   * from it toward the current pointer direction; otherwise from the origin.
   * Returns an error message or null on success.
   */
  submitRectangleSize(widthText: string, depthText?: string): string | null {
    const unit = this.editor.doc.displayUnit;
    let w: number, d: number;
    if (depthText === undefined || depthText.trim() === '') {
      const list = parseLengthList(widthText, 2, unit);
      if (!list.ok) return list.error;
      [w, d] = list.values as [number, number];
    } else {
      const pw = parseLength(widthText, unit);
      if (!pw.ok) return `Width: ${pw.error}`;
      const pd = parseLength(depthText, unit);
      if (!pd.ok) return `Depth: ${pd.error}`;
      w = pw.meters;
      d = pd.meters;
    }
    const preview = this.editor.getState().preview;
    const start: Vec3 = this.rect?.start ?? [0, 0, 0];
    let sx = 1, sy = 1;
    if (preview?.kind === 'rectangle') {
      if (preview.end[0] < preview.start[0]) sx = -1;
      if (preview.end[1] < preview.start[1]) sy = -1;
    }
    if (w < 0 || d < 0) return 'Width and depth must be positive; the drawing direction follows the pointer.';
    const r = rectangleFromSignedSize(start, sx * w, sy * d);
    if (!r.ok) return r.error;
    return this.commitRectangle(r.value) ? null : this.editor.getState().message?.text ?? 'Could not create rectangle.';
  }

  // ---- Push/Pull ----------------------------------------------------------

  /** Returns an explanation if the face cannot be push/pulled in this milestone. */
  static pushableReason(face: FaceId): string | null {
    if (face === 'profile' || face === 'cap') return null;
    if (face === 'base') return 'Push/pull of the base face is not supported yet. Pick the top cap to change height.';
    return `Push/pull of side faces (${faceLabel(face)}) is not supported yet. Pick the top cap to change height.`;
  }

  private pushDown(input: PointerInput): void {
    if (this.push) {
      this.pushFinish();
      return;
    }
    if (!input.pick) {
      this.editor.select(null);
      return;
    }
    const { objectId, faceId, point } = input.pick;
    const reason = ToolController.pushableReason(faceId);
    this.editor.select({ objectId, faceId });
    if (reason) {
      this.editor.notify({ kind: 'error', text: reason });
      return;
    }
    const obj = findObject(this.editor.doc, objectId);
    if (!obj) return;
    const startHeight = obj.extrusion?.height ?? 0;
    this.push = {
      objectId,
      face: faceId as 'profile' | 'cap',
      anchor: point,
      startHeight,
      pressScreen: input.screen,
      pressed: true,
    };
    this.editor.notify(null);
    this.setPushPreview(0);
  }

  private setPushPreview(delta: number): void {
    if (!this.push) return;
    const height = this.push.startHeight + delta;
    this.editor.setPreview({
      kind: 'pushpull',
      objectId: this.push.objectId,
      face: this.push.face,
      startHeight: this.push.startHeight,
      delta,
      height,
      error: checkHeight(height),
    });
  }

  private pushMove(input: PointerInput): void {
    if (!this.push) {
      const pick = input.pick;
      this.editor.setHover(pick && !ToolController.pushableReason(pick.faceId) ? { objectId: pick.objectId, faceId: pick.faceId } : null);
      return;
    }
    const t = closestParameterOnAxis(this.push.anchor, [0, 0, 1], input.ray);
    if (t === null) return;
    const { enabled, step } = this.editor.getState().snap;
    // Snap the resulting total height to the grid increment.
    const total = enabled ? snapValue(this.push.startHeight + t, step) : this.push.startHeight + t;
    this.setPushPreview(total - this.push.startHeight);
  }

  private pushFinish(): void {
    const preview = this.editor.getState().preview;
    if (!this.push || preview?.kind !== 'pushpull') return;
    if (Math.abs(preview.delta) < 1e-12) {
      // A zero-distance gesture is a no-op, not an edit.
      this.cancel();
      this.editor.notify({ kind: 'info', text: 'Push/pull cancelled (zero distance).' });
      return;
    }
    this.commitHeight(this.push.objectId, this.push.face, preview.height);
  }

  private commitHeight(objectId: string, face: 'profile' | 'cap', height: number): boolean {
    const label = face === 'profile' ? `Push/pull to ${formatLength(height, this.editor.doc.displayUnit)}` : `Set height to ${formatLength(height, this.editor.doc.displayUnit)}`;
    const ok = this.editor.commit(
      label,
      (doc) => (face === 'profile' ? extrudeProfile(doc, objectId, height) : setExtrusionHeight(doc, objectId, height)),
      () => ({ objectId, faceId: 'cap' }),
    );
    if (ok) {
      this.push = null;
      this.editor.cancelPreview();
    }
    return ok;
  }

  /**
   * Exact push/pull distance. This is an incremental distance from the face's
   * current position (for a planar face it equals the total height). It applies
   * to the active gesture, or else to the selected object.
   */
  submitPushDistance(text: string): string | null {
    const parsed = parseLength(text, this.editor.doc.displayUnit);
    if (!parsed.ok) return parsed.error;
    let objectId: string, face: 'profile' | 'cap', start: number;
    if (this.push) {
      ({ objectId, face } = this.push);
      start = this.push.startHeight;
    } else {
      const sel = this.editor.getState().selection;
      const obj = sel && findObject(this.editor.doc, sel.objectId);
      if (!obj) return 'Select a rectangle or an extrusion cap first.';
      if (sel.faceId && sel.faceId !== 'profile' && sel.faceId !== 'cap') {
        return ToolController.pushableReason(sel.faceId);
      }
      objectId = obj.id;
      face = obj.extrusion ? 'cap' : 'profile';
      start = obj.extrusion?.height ?? 0;
    }
    const err = checkHeight(start + parsed.meters);
    if (err) return err;
    if (parsed.meters === 0) return 'Distance must not be zero.';
    return this.commitHeight(objectId, face, start + parsed.meters) ? null : this.editor.getState().message?.text ?? 'Push/pull failed.';
  }

  /** Sets the total extrusion height of an object (from the properties panel). */
  setTotalHeight(objectId: string, text: string): string | null {
    const parsed = parseLength(text, this.editor.doc.displayUnit);
    if (!parsed.ok) return parsed.error;
    const obj = findObject(this.editor.doc, objectId);
    if (!obj) return 'Object no longer exists.';
    if (obj.extrusion && Math.abs(obj.extrusion.height - parsed.meters) < 1e-12) return null;
    return this.commitHeight(objectId, obj.extrusion ? 'cap' : 'profile', parsed.meters) ? null : this.editor.getState().message?.text ?? 'Could not set height.';
  }
}
