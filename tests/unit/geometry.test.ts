import * as THREE from 'three';
import { describe, expect, it } from 'vitest';
import { rectangleFromCorners, rectangleFromSignedSize } from '../../src/geometry/rectangle';
import { buildRenderData, checkSolid, faceForTriangle, objectBounds } from '../../src/geometry/solid';
import { createRectangle, extrudeProfile, setExtrusionHeight } from '../../src/model/operations';
import { createEmptyDocument, findObject } from '../../src/model/types';

function table() {
  const placement = rectangleFromSignedSize([0, 0, 0], 2, 1);
  if (!placement.ok) throw new Error(placement.error);
  const a = createRectangle(createEmptyDocument(), placement.value);
  if (!a.ok) throw new Error(a.error);
  const b = extrudeProfile(a.doc, a.objectId!, 0.75);
  if (!b.ok) throw new Error(b.error);
  return { doc: b.doc, id: a.objectId! };
}

describe('rectangle construction', () => {
  it('normalizes any drawing direction to positive size', () => {
    for (const [a, b] of [
      [[0, 0, 0], [2, 1, 0]],
      [[2, 1, 0], [0, 0, 0]],
      [[2, 0, 0], [0, 1, 0]],
      [[0, 1, 0], [2, 0, 0]],
    ] as const) {
      const r = rectangleFromCorners([...a], [...b]);
      expect(r.ok).toBe(true);
      if (r.ok) expect(r.value).toEqual({ corner: [0, 0, 0], width: 2, depth: 1 });
    }
  });

  it('rejects degenerate and non-finite rectangles', () => {
    expect(rectangleFromCorners([0, 0, 0], [2, 0, 0]).ok).toBe(false);
    expect(rectangleFromCorners([0, 0, 0], [0, 0, 0]).ok).toBe(false);
    expect(rectangleFromCorners([0, 0, 0], [NaN, 1, 0]).ok).toBe(false);
    expect(rectangleFromCorners([0, 0, 0], [Infinity, 1, 0]).ok).toBe(false);
    expect(rectangleFromSignedSize([0, 0, 0], 0, 1).ok).toBe(false);
    expect(rectangleFromSignedSize([0, 0, 0], 1e-6, 1).ok).toBe(false);
  });

  it('keeps a planar rectangle face two-sided with explicit back faces', () => {
    const p = rectangleFromSignedSize([0, 0, 0], 2, 1);
    if (!p.ok) throw new Error();
    const r = createRectangle(createEmptyDocument(), p.value);
    if (!r.ok) throw new Error();
    const data = buildRenderData(r.doc.objects[0]!);
    expect(data.faces).toEqual(['profile']);
    expect(data.closed).toBe(false);
    expect(data.indices.length / 3).toBe(4);
    expect(new Set(data.triangleFaces)).toEqual(new Set(['profile']));
    expect(data.edges.length / 6).toBe(4);
  });
});

describe('extrusion (2000 x 1000 x 750 mm)', () => {
  it('has model-space bounds of 2 m x 1 m x 0.75 m', () => {
    const { doc, id } = table();
    const b = objectBounds(findObject(doc, id)!);
    expect(b.min).toEqual([0, 0, 0]);
    expect(b.max[0]).toBeCloseTo(2, 12);
    expect(b.max[1]).toBeCloseTo(1, 12);
    expect(b.max[2]).toBeCloseTo(0.75, 12);
  });

  it('is a closed, consistently outward-wound prism with six logical faces', () => {
    const { doc, id } = table();
    const data = buildRenderData(findObject(doc, id)!);
    expect(data.faces).toEqual(['base', 'cap', 'side-0', 'side-1', 'side-2', 'side-3']);
    expect(data.indices.length / 3).toBe(12);
    const check = checkSolid(data);
    expect(check.problems).toEqual([]);
    expect(check.signedVolume).toBeCloseTo(1.5, 6);
    // 12 boundary edges, no diagonals.
    expect(data.edges.length / 6).toBe(12);
  });

  it('detects a mesh with a flipped face', () => {
    const { doc, id } = table();
    const data = buildRenderData(findObject(doc, id)!);
    const idx = data.indices;
    [idx[0], idx[1]] = [idx[1]!, idx[0]!];
    expect(checkSolid(data).problems.length).toBeGreaterThan(0);
  });

  it('resolves both triangles of the cap to the same logical face, including via a real raycast', () => {
    const { doc, id } = table();
    const data = buildRenderData(findObject(doc, id)!);
    const capTris = data.triangleFaces.map((f, i) => (f === 'cap' ? i : -1)).filter((i) => i >= 0);
    expect(capTris.length).toBe(2);
    expect(capTris.map((t) => faceForTriangle(data, t))).toEqual(['cap', 'cap']);

    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute('position', new THREE.BufferAttribute(data.positions, 3));
    geometry.setIndex(new THREE.BufferAttribute(data.indices, 1));
    const mesh = new THREE.Mesh(geometry, new THREE.MeshBasicMaterial({ side: THREE.FrontSide }));
    mesh.updateMatrixWorld();
    const raycaster = new THREE.Raycaster();
    const hits = new Set<number>();
    // Points on either side of the cap's triangulation diagonal.
    for (const [x, y] of [[1.8, 0.1], [0.2, 0.9]]) {
      raycaster.set(new THREE.Vector3(x, y, 5), new THREE.Vector3(0, 0, -1));
      const hit = raycaster.intersectObject(mesh)[0]!;
      hits.add(hit.faceIndex!);
      expect(faceForTriangle(data, hit.faceIndex!)).toBe('cap');
    }
    expect(hits.size).toBe(2);
    // Front-side-only material: a ray from inside does not hit the cap (faces really point outward).
    raycaster.set(new THREE.Vector3(1, 0.5, 0.3), new THREE.Vector3(0, 0, 1));
    expect(raycaster.intersectObject(mesh).length).toBe(0);
  });

  it('updates the same object when the height changes', () => {
    const { doc, id } = table();
    const r = setExtrusionHeight(doc, id, 0.9);
    expect(r.ok).toBe(true);
    if (!r.ok) return;
    expect(r.doc.objects.length).toBe(1);
    expect(r.doc.objects[0]!.id).toBe(id);
    expect(objectBounds(r.doc.objects[0]!).max[2]).toBeCloseTo(0.9, 12);
    expect(buildRenderData(r.doc.objects[0]!).faces).toContain('cap');
    expect(checkSolid(buildRenderData(r.doc.objects[0]!)).problems).toEqual([]);
  });

  it('rejects zero, negative and non-finite heights without changing the document', () => {
    const { doc, id } = table();
    for (const h of [0, -0.1, NaN, Infinity, 1e-6]) {
      const r = setExtrusionHeight(doc, id, h);
      expect(r.ok, String(h)).toBe(false);
    }
    expect(findObject(doc, id)!.extrusion!.height).toBe(0.75);
    expect(setExtrusionHeight(doc, 'obj-99', 1).ok).toBe(false);
    expect(extrudeProfile(doc, id, 1).ok).toBe(false); // already extruded
  });
});
