import { add, cross, dot, normalize, scale, sub, type Vec2, type Vec3 } from '../core/vec';
import type { FaceId, ModelObject, PlaneBasis } from '../model/types';

/**
 * Derived render data for one object, in object-local coordinates (apply
 * `transform.translation` to place it). Every triangle records the semantic
 * face it belongs to, so picks resolve to logical faces.
 */
export interface RenderData {
  positions: Float32Array; // xyz per vertex (faces do not share vertices, for flat shading)
  normals: Float32Array;
  indices: Uint32Array;
  /** triangleFaces[t] is the logical face of triangle t (indices 3t..3t+2). */
  triangleFaces: FaceId[];
  /** Logical faces in this object, in generation order. */
  faces: FaceId[];
  /** Boundary edges as line-segment pairs (xyz xyz). No triangulation diagonals. */
  edges: Float32Array;
  /** True when the faces form a closed solid. */
  closed: boolean;
}

export interface FacePolygon {
  id: FaceId;
  /** Outer loop, counter-clockwise when viewed from outside (along -normal). */
  loop: Vec3[];
  normal: Vec3;
}

function toWorld(plane: PlaneBasis, p: Vec2, offset: number, normal: Vec3): Vec3 {
  return add(add(add(plane.origin, scale(plane.xAxis, p[0])), scale(plane.yAxis, p[1])), scale(normal, offset));
}

/**
 * Builds the logical faces of an object. For an extrusion: base (facing -normal),
 * cap (facing +normal) and one side per boundary edge, all wound counter-clockwise
 * when seen from outside. A planar profile yields a single face.
 */
export function buildFaces(obj: ModelObject, heightOverride?: number): FacePolygon[] {
  const { plane, boundary } = obj.profile;
  const n = normalize(cross(plane.xAxis, plane.yAxis));
  const height = heightOverride ?? obj.extrusion?.height;
  const bottom = boundary.map((p) => toWorld(plane, p, 0, n));
  if (height === undefined) {
    return [{ id: 'profile', loop: bottom, normal: n }];
  }
  const top = boundary.map((p) => toWorld(plane, p, height, n));
  const faces: FacePolygon[] = [
    { id: 'base', loop: [...bottom].reverse(), normal: scale(n, -1) },
    { id: 'cap', loop: top, normal: n },
  ];
  for (let i = 0; i < boundary.length; i++) {
    const j = (i + 1) % boundary.length;
    const a = bottom[i]!, b = bottom[j]!, c = top[j]!, d = top[i]!;
    // CCW boundary: the interior is on the left of a→b, so (b-a)×n points outward.
    faces.push({ id: `side-${i}`, loop: [a, b, c, d], normal: normalize(cross(sub(b, a), n)) });
  }
  return faces;
}

/** Fan triangulation; valid for the convex profiles supported in this milestone. */
function triangulateConvex(count: number): [number, number, number][] {
  const tris: [number, number, number][] = [];
  for (let i = 1; i < count - 1; i++) tris.push([0, i, i + 1]);
  return tris;
}

export function buildRenderData(obj: ModelObject, heightOverride?: number): RenderData {
  const faces = buildFaces(obj, heightOverride);
  const closed = faces.length > 1;
  const positions: number[] = [];
  const normals: number[] = [];
  const indices: number[] = [];
  const triangleFaces: FaceId[] = [];

  const emit = (face: FacePolygon, loop: Vec3[], normal: Vec3) => {
    const base = positions.length / 3;
    for (const p of loop) {
      positions.push(...p);
      normals.push(...normal);
    }
    for (const [a, b, c] of triangulateConvex(loop.length)) {
      indices.push(base + a, base + b, base + c);
      triangleFaces.push(face.id);
    }
  };

  for (const face of faces) {
    emit(face, face.loop, face.normal);
    // A planar profile has no inside: it is a two-sided sheet, so it gets an explicit
    // back face with reversed winding instead of relying on double-sided materials.
    if (!closed) emit(face, [...face.loop].reverse(), scale(face.normal, -1));
  }

  const edgeKey = (a: Vec3, b: Vec3) => {
    const ka = a.join(','), kb = b.join(',');
    return ka < kb ? `${ka}|${kb}` : `${kb}|${ka}`;
  };
  const edgeSet = new Map<string, [Vec3, Vec3]>();
  for (const face of faces) {
    face.loop.forEach((p, i) => {
      const q = face.loop[(i + 1) % face.loop.length]!;
      edgeSet.set(edgeKey(p, q), [p, q]);
    });
  }
  const edges: number[] = [];
  for (const [a, b] of edgeSet.values()) edges.push(...a, ...b);

  return {
    positions: new Float32Array(positions),
    normals: new Float32Array(normals),
    indices: new Uint32Array(indices),
    triangleFaces,
    faces: faces.map((f) => f.id),
    edges: new Float32Array(edges),
    closed,
  };
}

/** Maps a picked triangle index back to its logical face. */
export function faceForTriangle(data: RenderData, triangleIndex: number): FaceId | null {
  return data.triangleFaces[triangleIndex] ?? null;
}

export interface Bounds {
  min: Vec3;
  max: Vec3;
}

/** Model-space (world) bounds of an object. */
export function objectBounds(obj: ModelObject): Bounds {
  const min: Vec3 = [Infinity, Infinity, Infinity];
  const max: Vec3 = [-Infinity, -Infinity, -Infinity];
  for (const face of buildFaces(obj)) {
    for (const local of face.loop) {
      const p = add(local, obj.transform.translation);
      for (let k = 0; k < 3; k++) {
        min[k] = Math.min(min[k]!, p[k]!);
        max[k] = Math.max(max[k]!, p[k]!);
      }
    }
  }
  return { min, max };
}

export function unionBounds(list: Bounds[]): Bounds | null {
  if (list.length === 0) return null;
  const min: Vec3 = [Infinity, Infinity, Infinity];
  const max: Vec3 = [-Infinity, -Infinity, -Infinity];
  for (const b of list) {
    for (let k = 0; k < 3; k++) {
      min[k] = Math.min(min[k]!, b.min[k]!);
      max[k] = Math.max(max[k]!, b.max[k]!);
    }
  }
  return { min, max };
}

export interface SolidCheck {
  closed: boolean;
  consistentWinding: boolean;
  normalsMatchWinding: boolean;
  signedVolume: number;
  problems: string[];
}

/**
 * Verifies a closed triangle mesh: every directed edge must be matched by exactly
 * one opposite directed edge (closed, consistently oriented, 2-manifold), triangle
 * winding must agree with stored normals, and the enclosed signed volume must be
 * positive (outward orientation).
 */
export function checkSolid(data: RenderData): SolidCheck {
  const problems: string[] = [];
  const pos = (i: number): Vec3 => [data.positions[3 * i]!, data.positions[3 * i + 1]!, data.positions[3 * i + 2]!];
  const key = (p: Vec3) => p.map((x) => x.toFixed(9)).join(',');
  const directed = new Map<string, number>();
  let volume = 0;
  let normalsMatch = true;
  for (let t = 0; t < data.indices.length / 3; t++) {
    const ids = [data.indices[3 * t]!, data.indices[3 * t + 1]!, data.indices[3 * t + 2]!];
    const [a, b, c] = ids.map(pos) as [Vec3, Vec3, Vec3];
    volume += dot(a, cross(b, c)) / 6;
    const geomNormal = cross(sub(b, a), sub(c, a));
    const i0 = ids[0]!;
    const stored: Vec3 = [data.normals[3 * i0]!, data.normals[3 * i0 + 1]!, data.normals[3 * i0 + 2]!];
    if (dot(geomNormal, stored) <= 0) normalsMatch = false;
    const ks = [a, b, c].map(key);
    for (let e = 0; e < 3; e++) {
      const k = `${ks[e]}>${ks[(e + 1) % 3]}`;
      directed.set(k, (directed.get(k) ?? 0) + 1);
    }
  }
  let closed = true;
  let consistent = true;
  for (const [k, count] of directed) {
    if (count > 1) consistent = false;
    const [from, to] = k.split('>');
    if (!directed.has(`${to}>${from}`)) closed = false;
  }
  if (!closed) problems.push('mesh has boundary edges (not closed)');
  if (!consistent) problems.push('mesh has inconsistently oriented or non-manifold edges');
  if (!normalsMatch) problems.push('stored normals disagree with triangle winding');
  if (volume <= 0) problems.push('signed volume is not positive (faces point inward)');
  return { closed, consistentWinding: consistent, normalsMatchWinding: normalsMatch, signedVolume: volume, problems };
}
