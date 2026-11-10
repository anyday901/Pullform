export type Vec2 = [number, number];
export type Vec3 = [number, number, number];

export const add = (a: Vec3, b: Vec3): Vec3 => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
export const sub = (a: Vec3, b: Vec3): Vec3 => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
export const scale = (a: Vec3, s: number): Vec3 => [a[0] * s, a[1] * s, a[2] * s];
export const dot = (a: Vec3, b: Vec3): number => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
export const cross = (a: Vec3, b: Vec3): Vec3 => [
  a[1] * b[2] - a[2] * b[1],
  a[2] * b[0] - a[0] * b[2],
  a[0] * b[1] - a[1] * b[0],
];
export const length = (a: Vec3): number => Math.hypot(a[0], a[1], a[2]);
export const normalize = (a: Vec3): Vec3 => {
  const l = length(a);
  return l === 0 ? [0, 0, 0] : scale(a, 1 / l);
};

export interface Ray {
  origin: Vec3;
  direction: Vec3; // unit length
}

/** Intersects a ray with the plane z = height. Returns null when parallel or behind the origin. */
export function intersectRayWithHorizontalPlane(ray: Ray, height = 0): Vec3 | null {
  const dz = ray.direction[2];
  if (Math.abs(dz) < 1e-12) return null;
  const t = (height - ray.origin[2]) / dz;
  if (t < 0) return null;
  return add(ray.origin, scale(ray.direction, t));
}

/**
 * Parameter t of the point on the line (point + t * axis) closest to the ray.
 * Used by push/pull to turn pointer movement into a distance along a face normal.
 * Returns null when the ray is (nearly) parallel to the axis.
 */
export function closestParameterOnAxis(point: Vec3, axis: Vec3, ray: Ray): number | null {
  const u = normalize(axis);
  const v = ray.direction;
  const w0 = sub(point, ray.origin);
  const a = dot(u, u);
  const b = dot(u, v);
  const c = dot(v, v);
  const d = dot(u, w0);
  const e = dot(v, w0);
  const denom = a * c - b * b;
  if (Math.abs(denom) < 1e-9) return null;
  return (b * e - c * d) / denom;
}
