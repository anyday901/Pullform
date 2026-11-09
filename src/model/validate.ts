import { OBJECT_ID_PATTERN, objectIdNumber } from '../core/ids';
import { isFiniteNumber, LENGTH_EPSILON, MAX_COORDINATE, MIN_DIMENSION } from '../core/tolerance';
import { LENGTH_UNITS, type LengthUnit } from '../core/units';
import type { Vec2, Vec3 } from '../core/vec';
import { rectangleSize } from '../geometry/rectangle';
import type { ModelDocument, ModelObject, PlaneBasis } from './types';

export type Validation<T> = { ok: true; value: T } | { ok: false; error: string };

class ValidationError extends Error {}

function fail(path: string, message: string): never {
  throw new ValidationError(`${path}: ${message}`);
}

function isRecord(v: unknown): v is Record<string, unknown> {
  return typeof v === 'object' && v !== null && !Array.isArray(v);
}

function num(v: unknown, path: string): number {
  if (!isFiniteNumber(v)) fail(path, 'must be a finite number');
  if (Math.abs(v) > MAX_COORDINATE) fail(path, `exceeds the supported range of ±${MAX_COORDINATE} m`);
  return v;
}

function vec3(v: unknown, path: string): Vec3 {
  if (!Array.isArray(v) || v.length !== 3) fail(path, 'must be an array of 3 numbers');
  return [num(v[0], `${path}[0]`), num(v[1], `${path}[1]`), num(v[2], `${path}[2]`)];
}

function vec2(v: unknown, path: string): Vec2 {
  if (!Array.isArray(v) || v.length !== 2) fail(path, 'must be an array of 2 numbers');
  return [num(v[0], `${path}[0]`), num(v[1], `${path}[1]`)];
}

function sameVec(a: Vec3, b: Vec3): boolean {
  return a.every((x, i) => Math.abs(x - b[i]!) <= LENGTH_EPSILON);
}

function plane(v: unknown, path: string): PlaneBasis {
  if (!isRecord(v)) fail(path, 'must be an object');
  const p: PlaneBasis = {
    origin: vec3(v.origin, `${path}.origin`),
    xAxis: vec3(v.xAxis, `${path}.xAxis`),
    yAxis: vec3(v.yAxis, `${path}.yAxis`),
  };
  // Milestone 1: profiles live on the object's local XY plane.
  if (!sameVec(p.origin, [0, 0, 0]) || !sameVec(p.xAxis, [1, 0, 0]) || !sameVec(p.yAxis, [0, 1, 0])) {
    fail(path, 'only the local XY plane (origin 0,0,0; xAxis 1,0,0; yAxis 0,1,0) is supported in this version');
  }
  return p;
}

/** Validates one object. Throws ValidationError with a path-qualified message. */
function object(v: unknown, path: string): ModelObject {
  if (!isRecord(v)) fail(path, 'must be an object');
  if (typeof v.id !== 'string' || !OBJECT_ID_PATTERN.test(v.id)) fail(`${path}.id`, 'must look like "obj-1"');
  if (v.type !== 'profile') fail(`${path}.type`, `unsupported object type ${JSON.stringify(v.type)}`);
  if (typeof v.name !== 'string' || v.name.length > 200) fail(`${path}.name`, 'must be a string (max 200 characters)');
  if (!isRecord(v.profile)) fail(`${path}.profile`, 'must be an object');
  if (v.profile.kind !== 'rectangle') fail(`${path}.profile.kind`, `unsupported profile kind ${JSON.stringify(v.profile.kind)}`);
  const pl = plane(v.profile.plane, `${path}.profile.plane`);
  if (!Array.isArray(v.profile.boundary)) fail(`${path}.profile.boundary`, 'must be an array');
  const boundary = v.profile.boundary.map((p, i) => vec2(p, `${path}.profile.boundary[${i}]`));
  const size = rectangleSize(boundary);
  if (!size.ok) fail(`${path}.profile.boundary`, size.error);

  let extrusion: ModelObject['extrusion'] = null;
  if (v.extrusion !== null) {
    if (!isRecord(v.extrusion)) fail(`${path}.extrusion`, 'must be null or an object');
    const h = num(v.extrusion.height, `${path}.extrusion.height`);
    if (h < MIN_DIMENSION) fail(`${path}.extrusion.height`, `must be at least ${MIN_DIMENSION * 1000} mm`);
    extrusion = { height: h };
  }
  if (!isRecord(v.transform)) fail(`${path}.transform`, 'must be an object');
  const translation = vec3(v.transform.translation, `${path}.transform.translation`);

  return {
    id: v.id,
    type: 'profile',
    name: v.name,
    profile: { kind: 'rectangle', plane: pl, boundary },
    extrusion,
    transform: { translation },
  };
}

/** Validates (and normalizes into a fresh object) an untrusted document. */
export function validateDocument(v: unknown): Validation<ModelDocument> {
  try {
    if (!isRecord(v)) fail('document', 'must be an object');
    if (v.lengthUnit !== 'm') fail('document.lengthUnit', 'must be "m"');
    if (typeof v.displayUnit !== 'string' || !LENGTH_UNITS.includes(v.displayUnit as LengthUnit)) {
      fail('document.displayUnit', `must be one of ${LENGTH_UNITS.join(', ')}`);
    }
    if (!Number.isSafeInteger(v.nextId) || (v.nextId as number) < 1) fail('document.nextId', 'must be a positive integer');
    if (!Array.isArray(v.objects)) fail('document.objects', 'must be an array');
    const objects = v.objects.map((o, i) => object(o, `document.objects[${i}]`));
    const seen = new Set<string>();
    for (const [i, o] of objects.entries()) {
      if (seen.has(o.id)) fail(`document.objects[${i}].id`, `duplicate ID ${o.id}`);
      seen.add(o.id);
      if (objectIdNumber(o.id)! >= (v.nextId as number)) {
        fail('document.nextId', `must be greater than the number in every object ID (found ${o.id})`);
      }
    }
    return {
      ok: true,
      value: { lengthUnit: 'm', displayUnit: v.displayUnit as LengthUnit, nextId: v.nextId as number, objects },
    };
  } catch (e) {
    if (e instanceof ValidationError) return { ok: false, error: e.message };
    throw e;
  }
}

/** Validates a single object produced by a modeling operation before it is committed. */
export function validateObject(o: ModelObject): Validation<ModelObject> {
  try {
    return { ok: true, value: object(o, o.id ?? 'object') };
  } catch (e) {
    if (e instanceof ValidationError) return { ok: false, error: e.message };
    throw e;
  }
}
