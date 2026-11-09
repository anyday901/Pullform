/**
 * Central geometry tolerances. All lengths are in meters (the internal unit).
 *
 * Supported scale range: dimensions from 0.1 mm (MIN_DIMENSION) up to
 * coordinates of ±10 km (MAX_COORDINATE). Double precision gives far more
 * resolution than this at that range; the limits exist so that degenerate or
 * absurd input is rejected before it reaches the document.
 */
export const LENGTH_EPSILON = 1e-9; // equality tolerance for coordinates (1 nm)
export const MIN_DIMENSION = 1e-4; // smallest accepted width/depth/height: 0.1 mm
export const MAX_COORDINATE = 1e4; // largest accepted |coordinate| or dimension: 10 km
export const MIN_AREA = MIN_DIMENSION * MIN_DIMENSION;

export function isFiniteNumber(value: unknown): value is number {
  return typeof value === 'number' && Number.isFinite(value);
}

export function nearlyEqual(a: number, b: number, eps = LENGTH_EPSILON): boolean {
  return Math.abs(a - b) <= eps;
}
