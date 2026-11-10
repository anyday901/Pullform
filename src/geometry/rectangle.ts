import { isFiniteNumber, LENGTH_EPSILON, MAX_COORDINATE, MIN_DIMENSION } from '../core/tolerance';
import type { Vec2, Vec3 } from '../core/vec';

export type Result<T> = { ok: true; value: T } | { ok: false; error: string };

export interface RectanglePlacement {
  /** Minimum corner (world), on the ground plane. */
  corner: Vec3;
  width: number; // along +X
  depth: number; // along +Y
}

/** Counter-clockwise (seen from +Z) boundary for a rectangle anchored at the local origin. */
export function rectangleBoundary(width: number, depth: number): Vec2[] {
  return [
    [0, 0],
    [width, 0],
    [width, depth],
    [0, depth],
  ];
}

function checkLength(value: number, label: string): string | null {
  if (!isFiniteNumber(value)) return `${label} must be a finite number.`;
  if (Math.abs(value) < MIN_DIMENSION) return `${label} must be at least ${MIN_DIMENSION * 1000} mm.`;
  if (Math.abs(value) > MAX_COORDINATE) return `${label} exceeds the supported maximum of ${MAX_COORDINATE / 1000} km.`;
  return null;
}

/**
 * Normalizes a rectangle given by two opposite corners on the ground plane, in
 * any drawing direction, into a min-corner placement with positive dimensions.
 */
export function rectangleFromCorners(a: Vec3, b: Vec3): Result<RectanglePlacement> {
  for (const v of [...a, ...b]) {
    if (!isFiniteNumber(v)) return { ok: false, error: 'Corner coordinates must be finite numbers.' };
    if (Math.abs(v) > MAX_COORDINATE) return { ok: false, error: 'Corner is outside the supported model range.' };
  }
  const width = Math.abs(b[0] - a[0]);
  const depth = Math.abs(b[1] - a[1]);
  if (width < MIN_DIMENSION || depth < MIN_DIMENSION) {
    return { ok: false, error: `Rectangle is degenerate: width and depth must each be at least ${MIN_DIMENSION * 1000} mm.` };
  }
  return {
    ok: true,
    value: { corner: [Math.min(a[0], b[0]), Math.min(a[1], b[1]), a[2]], width, depth },
  };
}

/**
 * Builds a placement from a start corner and signed width/depth. Signs choose
 * the drawing direction (so typed dimensions follow the mouse direction).
 */
export function rectangleFromSignedSize(start: Vec3, width: number, depth: number): Result<RectanglePlacement> {
  const err = checkLength(width, 'Width') ?? checkLength(depth, 'Depth');
  if (err) return { ok: false, error: err };
  return rectangleFromCorners(start, [start[0] + width, start[1] + depth, start[2]]);
}

/**
 * Checks that a boundary is an axis-aligned, counter-clockwise rectangle anchored at
 * the local origin, and returns its size.
 */
export function rectangleSize(boundary: Vec2[]): Result<{ width: number; depth: number }> {
  if (boundary.length !== 4) return { ok: false, error: 'a rectangle boundary must have exactly 4 points' };
  const [p0, p1, p2, p3] = boundary as [Vec2, Vec2, Vec2, Vec2];
  const eq = (x: number, y: number) => Math.abs(x - y) <= LENGTH_EPSILON;
  const width = p1[0];
  const depth = p3[1];
  const isRect =
    eq(p0[0], 0) && eq(p0[1], 0) &&
    eq(p1[1], 0) &&
    eq(p2[0], width) && eq(p2[1], depth) &&
    eq(p3[0], 0);
  if (!isRect) return { ok: false, error: 'must be the counter-clockwise rectangle [0,0],[w,0],[w,d],[0,d]' };
  const err = checkLength(width, 'width') ?? checkLength(depth, 'depth');
  if (err) return { ok: false, error: `rectangle is invalid: ${err}` };
  if (width < 0 || depth < 0) return { ok: false, error: 'boundary must be counter-clockwise with positive width and depth' };
  return { ok: true, value: { width, depth } };
}
