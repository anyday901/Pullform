import { buildFaces } from '../geometry/solid';
import { findObject, type FaceId, type ModelDocument } from '../model/types';

/** Selection is a whole object, optionally with the targeted logical face. */
export type Selection = { objectId: string; faceId: FaceId | null } | null;

/** Drops references that no longer exist in the document (e.g. after undo). */
export function sanitizeSelection(doc: ModelDocument, sel: Selection): Selection {
  if (!sel) return null;
  const obj = findObject(doc, sel.objectId);
  if (!obj) return null;
  if (sel.faceId && !buildFaces(obj).some((f) => f.id === sel.faceId)) {
    // A planar profile's face becomes the cap when extruded and vice versa.
    if (sel.faceId === 'profile' && obj.extrusion) return { objectId: obj.id, faceId: 'cap' };
    if (sel.faceId === 'cap' && !obj.extrusion) return { objectId: obj.id, faceId: 'profile' };
    return { objectId: obj.id, faceId: null };
  }
  return sel;
}

export function faceLabel(face: FaceId | null): string {
  if (!face) return 'Whole object';
  if (face === 'profile') return 'Face';
  if (face === 'base') return 'Base';
  if (face === 'cap') return 'Cap (top)';
  return `Side ${Number(face.slice(5)) + 1}`;
}
