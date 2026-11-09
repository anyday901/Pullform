import * as THREE from 'three';

/** Ground grid on the XY plane (Z-up). Minor lines every 100 mm near the origin, major every 1 m. */
export function createGrid(): THREE.Group {
  const group = new THREE.Group();
  group.name = 'grid';
  const lines = (step: number, extent: number, color: number, opacity: number) => {
    const pts: number[] = [];
    const n = Math.round(extent / step);
    for (let i = -n; i <= n; i++) {
      const v = i * step;
      if (Math.abs(v) < 1e-9) continue; // axes are drawn separately
      pts.push(-extent, v, 0, extent, v, 0, v, -extent, 0, v, extent, 0);
    }
    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.Float32BufferAttribute(pts, 3));
    const mat = new THREE.LineBasicMaterial({ color, transparent: true, opacity, depthWrite: false });
    const seg = new THREE.LineSegments(geo, mat);
    // Drawn first and without depth writes so faces on the ground plane cover it.
    seg.renderOrder = -2;
    return seg;
  };
  group.add(lines(0.1, 5, 0x9aa3ad, 0.25));
  group.add(lines(1, 25, 0x7d8792, 0.4));
  return group;
}

/** Red X, green Y, blue Z axes; negative halves are dimmer. */
export function createAxes(length = 50): THREE.Group {
  const group = new THREE.Group();
  group.name = 'axes';
  const axis = (dir: [number, number, number], color: number, opacity: number) => {
    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.Float32BufferAttribute([0, 0, 0, dir[0] * length, dir[1] * length, dir[2] * length], 3));
    const line = new THREE.Line(geo, new THREE.LineBasicMaterial({ color, transparent: opacity < 1, opacity, depthWrite: false }));
    line.renderOrder = -1;
    group.add(line);
  };
  axis([1, 0, 0], 0xd32f2f, 1);
  axis([0, 1, 0], 0x2e7d32, 1);
  axis([0, 0, 1], 0x1565c0, 1);
  axis([-1, 0, 0], 0xd32f2f, 0.3);
  axis([0, -1, 0], 0x2e7d32, 0.3);
  axis([0, 0, -1], 0x1565c0, 0.3);
  return group;
}

export function disposeObject3D(root: THREE.Object3D, disposeMaterials = false): void {
  root.traverse((o) => {
    const anyO = o as THREE.Mesh;
    anyO.geometry?.dispose?.();
    if (disposeMaterials) {
      const m = anyO.material as THREE.Material | THREE.Material[] | undefined;
      if (Array.isArray(m)) m.forEach((x) => x.dispose());
      else m?.dispose?.();
    }
  });
}
