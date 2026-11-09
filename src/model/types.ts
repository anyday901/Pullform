import type { LengthUnit } from '../core/units';
import type { Vec2, Vec3 } from '../core/vec';

/**
 * Canonical, editable model data. Render meshes are derived from this and are
 * never stored as model truth.
 *
 * Coordinate convention: right-handed, Z-up, meters. The ground plane is XY at z = 0.
 */

/** A planar coordinate frame. The plane normal is xAxis × yAxis. */
export interface PlaneBasis {
  origin: Vec3;
  xAxis: Vec3;
  yAxis: Vec3;
}

/**
 * Milestone 1 only supports axis-aligned rectangles on the object's local XY plane.
 * The boundary is an ordered, counter-clockwise loop (seen from +normal) in plane
 * coordinates. A future `kind: 'polygon'` can reuse the same fields.
 */
export interface RectangleProfile {
  kind: 'rectangle';
  plane: PlaneBasis;
  boundary: Vec2[];
}

export interface Extrusion {
  /** Total extrusion distance along the profile normal. Always positive. */
  height: number;
}

/** Object placement. Milestone 1 supports translation only. */
export interface Transform {
  translation: Vec3;
}

export interface ProfileObject {
  id: string;
  type: 'profile';
  name: string;
  profile: RectangleProfile;
  /** null = a planar face; otherwise a closed prism. */
  extrusion: Extrusion | null;
  transform: Transform;
}

export type ModelObject = ProfileObject;

export interface ModelDocument {
  /** Internal length unit. Always meters. */
  lengthUnit: 'm';
  /** Unit used for display and unitless input. */
  displayUnit: LengthUnit;
  /** Next number used to allocate a stable object ID. */
  nextId: number;
  objects: ModelObject[];
}

/**
 * Semantic face IDs. Picking resolves triangles to these, so a rectangular face
 * is one logical face even though it is rendered as two triangles.
 * - `profile`: the single face of an un-extruded rectangle
 * - `base` / `cap`: bottom and top of an extrusion
 * - `side-N`: the side generated from boundary edge N (from vertex N to N+1)
 */
export type FaceId = 'profile' | 'base' | 'cap' | `side-${number}`;

export function createEmptyDocument(displayUnit: LengthUnit = 'mm'): ModelDocument {
  return { lengthUnit: 'm', displayUnit, nextId: 1, objects: [] };
}

export function findObject(doc: ModelDocument, id: string): ModelObject | undefined {
  return doc.objects.find((o) => o.id === id);
}

export const XY_PLANE: PlaneBasis = { origin: [0, 0, 0], xAxis: [1, 0, 0], yAxis: [0, 1, 0] };
