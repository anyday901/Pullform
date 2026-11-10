import { formatObjectId } from '../core/ids';
import { isFiniteNumber, MAX_COORDINATE, MIN_DIMENSION } from '../core/tolerance';
import { formatLength } from '../core/units';
import { rectangleBoundary, type RectanglePlacement } from '../geometry/rectangle';
import { findObject, XY_PLANE, type ModelDocument, type ModelObject } from './types';
import { validateObject } from './validate';

/**
 * Pure modeling operations. Each returns a new document (unchanged objects are
 * shared) or an error, and never mutates its input. The editor commits these
 * through a single path that records history.
 */
export type OpResult = { ok: true; doc: ModelDocument; objectId?: string } | { ok: false; error: string };

function replaceObject(doc: ModelDocument, next: ModelObject): OpResult {
  const checked = validateObject(next);
  if (!checked.ok) return { ok: false, error: checked.error };
  return {
    ok: true,
    doc: { ...doc, objects: doc.objects.map((o) => (o.id === next.id ? checked.value : o)) },
    objectId: next.id,
  };
}

export function checkHeight(height: number): string | null {
  if (!isFiniteNumber(height)) return 'Height must be a finite number.';
  if (height <= 0) {
    return 'Height must be positive. Pushing a face below its base (negative or zero height) is not supported yet.';
  }
  if (height < MIN_DIMENSION) return `Height must be at least ${formatLength(MIN_DIMENSION)}.`;
  if (height > MAX_COORDINATE) return `Height exceeds the supported maximum of ${MAX_COORDINATE / 1000} km.`;
  return null;
}

export function createRectangle(doc: ModelDocument, placement: RectanglePlacement): OpResult {
  const id = formatObjectId(doc.nextId);
  const obj: ModelObject = {
    id,
    type: 'profile',
    name: `Rectangle ${doc.nextId}`,
    profile: { kind: 'rectangle', plane: structuredClone(XY_PLANE), boundary: rectangleBoundary(placement.width, placement.depth) },
    extrusion: null,
    transform: { translation: [...placement.corner] },
  };
  const checked = validateObject(obj);
  if (!checked.ok) return { ok: false, error: checked.error };
  return { ok: true, doc: { ...doc, nextId: doc.nextId + 1, objects: [...doc.objects, checked.value] }, objectId: id };
}

/** Push/pull a planar rectangle into a prism of the given total height. */
export function extrudeProfile(doc: ModelDocument, id: string, height: number): OpResult {
  const obj = findObject(doc, id);
  if (!obj) return { ok: false, error: `Object ${id} does not exist.` };
  if (obj.extrusion) return { ok: false, error: `${obj.name} is already extruded; edit its cap height instead.` };
  const err = checkHeight(height);
  if (err) return { ok: false, error: err };
  return replaceObject(doc, { ...obj, extrusion: { height } });
}

/** Set the total height of an existing extrusion (moves its cap). Updates the same body. */
export function setExtrusionHeight(doc: ModelDocument, id: string, height: number): OpResult {
  const obj = findObject(doc, id);
  if (!obj) return { ok: false, error: `Object ${id} does not exist.` };
  if (!obj.extrusion) return { ok: false, error: `${obj.name} is not extruded yet; push/pull its face first.` };
  const err = checkHeight(height);
  if (err) return { ok: false, error: err };
  return replaceObject(doc, { ...obj, extrusion: { height } });
}

export function deleteObject(doc: ModelDocument, id: string): OpResult {
  if (!findObject(doc, id)) return { ok: false, error: `Object ${id} does not exist.` };
  return { ok: true, doc: { ...doc, objects: doc.objects.filter((o) => o.id !== id) } };
}
