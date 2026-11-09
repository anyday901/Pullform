import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import type { Vec3 } from '../core/vec';
import type { Editor, EditorState } from '../editor/Editor';
import { buildRenderData, objectBounds, unionBounds, type RenderData } from '../geometry/solid';
import { findObject, type FaceId, type ModelObject } from '../model/types';
import type { CameraView } from '../persistence/projectFile';
import type { PointerInput, ToolController } from '../tools/ToolController';
import { createAxes, createGrid, disposeObject3D } from './sceneHelpers';

const COLORS = {
  background: 0xf3f4f6,
  face: 0xe4dfd3,
  edge: 0x1f2328,
  selectedEdge: 0x1a73e8,
  selectedFace: 0x1a73e8,
  hoverFace: 0xffb300,
  preview: 0x1a73e8,
  invalid: 0xd32f2f,
};

interface ObjectView {
  source: ModelObject;
  group: THREE.Group;
  mesh: THREE.Mesh;
  edges: THREE.LineSegments;
  data: RenderData;
}

export const DEFAULT_VIEW: CameraView = { position: [5.5, -7, 4.5], target: [1, 0.5, 0] };

/**
 * Three.js viewport. Renders meshes derived from the document, maps picks back
 * to (objectId, logical face), and forwards pointer input to the tool
 * controller. Camera navigation never touches model history.
 *
 * Mouse: left = active tool, middle drag = orbit (Shift+middle = pan),
 * right drag = pan, wheel = zoom toward cursor. With the Orbit tool, left drag orbits.
 */
export class Viewport {
  readonly renderer: THREE.WebGLRenderer;
  readonly scene = new THREE.Scene();
  readonly camera: THREE.PerspectiveCamera;
  readonly controls: OrbitControls;
  private readonly raycaster = new THREE.Raycaster();
  private readonly views = new Map<string, ObjectView>();
  private readonly overlay = new THREE.Group();
  private readonly materials = {
    face: new THREE.MeshStandardMaterial({ color: COLORS.face, roughness: 0.92, metalness: 0, side: THREE.FrontSide, polygonOffset: true, polygonOffsetFactor: 1, polygonOffsetUnits: 1 }),
    edge: new THREE.LineBasicMaterial({ color: COLORS.edge }),
    selectedEdge: new THREE.LineBasicMaterial({ color: COLORS.selectedEdge }),
    selectedFace: new THREE.MeshBasicMaterial({ color: COLORS.selectedFace, transparent: true, opacity: 0.32, depthWrite: false, side: THREE.FrontSide, polygonOffset: true, polygonOffsetFactor: -1, polygonOffsetUnits: -1 }),
    hoverFace: new THREE.MeshBasicMaterial({ color: COLORS.hoverFace, transparent: true, opacity: 0.3, depthWrite: false, side: THREE.FrontSide, polygonOffset: true, polygonOffsetFactor: -1, polygonOffsetUnits: -1 }),
    previewEdge: new THREE.LineBasicMaterial({ color: COLORS.preview }),
    previewFill: new THREE.MeshBasicMaterial({ color: COLORS.preview, transparent: true, opacity: 0.18, depthWrite: false, side: THREE.DoubleSide }),
    invalidEdge: new THREE.LineBasicMaterial({ color: COLORS.invalid }),
  };
  private readonly grid = createGrid();
  private readonly axes = createAxes();
  private readonly resizeObserver: ResizeObserver;
  private readonly unsubscribe: () => void;
  private last: EditorState | null = null;
  private frame = 0;
  private cameraDrag = false;
  private disposed = false;

  constructor(private readonly container: HTMLElement, private readonly editor: Editor, private readonly tools: ToolController) {
    this.renderer = new THREE.WebGLRenderer({ antialias: true, preserveDrawingBuffer: false });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    this.renderer.setClearColor(COLORS.background);
    const canvas = this.renderer.domElement;
    canvas.className = 'viewport-canvas';
    canvas.setAttribute('aria-label', '3D modeling viewport');
    canvas.tabIndex = 0;
    container.appendChild(canvas);

    this.camera = new THREE.PerspectiveCamera(45, 1, 0.01, 5000);
    this.camera.up.set(0, 0, 1); // Z-up
    this.controls = new OrbitControls(this.camera, canvas);
    this.controls.enableDamping = false;
    this.controls.screenSpacePanning = true;
    this.controls.zoomToCursor = true;
    this.controls.minDistance = 0.05;
    this.controls.maxDistance = 2000;
    this.controls.addEventListener('change', this.requestRender);
    this.controls.addEventListener('start', this.onControlsStart);
    this.controls.addEventListener('end', this.onControlsEnd);
    this.setView(DEFAULT_VIEW);

    const sky = new THREE.HemisphereLight(0xffffff, 0x8d8d8d, 2.2);
    sky.position.set(0, 0, 1); // Z-up sky direction
    const sun = new THREE.DirectionalLight(0xffffff, 1.6);
    sun.position.set(3, -5, 8);
    this.scene.add(sky, sun);
    this.scene.add(this.grid, this.axes, this.overlay);

    canvas.addEventListener('pointerdown', this.onPointerDown);
    canvas.addEventListener('pointermove', this.onPointerMove);
    canvas.addEventListener('pointerup', this.onPointerUp);
    canvas.addEventListener('pointerleave', this.onPointerLeave);
    canvas.addEventListener('contextmenu', this.onContextMenu);

    this.resizeObserver = new ResizeObserver(() => this.resize());
    this.resizeObserver.observe(container);
    this.resize();

    this.unsubscribe = editor.subscribe(() => this.sync());
    this.sync();
  }

  // ---- Lifecycle ------------------------------------------------------------

  dispose(): void {
    this.disposed = true;
    cancelAnimationFrame(this.frame);
    this.unsubscribe();
    this.resizeObserver.disconnect();
    const canvas = this.renderer.domElement;
    canvas.removeEventListener('pointerdown', this.onPointerDown);
    canvas.removeEventListener('pointermove', this.onPointerMove);
    canvas.removeEventListener('pointerup', this.onPointerUp);
    canvas.removeEventListener('pointerleave', this.onPointerLeave);
    canvas.removeEventListener('contextmenu', this.onContextMenu);
    this.controls.removeEventListener('change', this.requestRender);
    this.controls.removeEventListener('start', this.onControlsStart);
    this.controls.removeEventListener('end', this.onControlsEnd);
    this.controls.dispose();
    for (const v of this.views.values()) disposeObject3D(v.group);
    this.views.clear();
    this.clearOverlay();
    disposeObject3D(this.grid, true);
    disposeObject3D(this.axes, true);
    Object.values(this.materials).forEach((m) => m.dispose());
    this.renderer.dispose();
    canvas.remove();
  }

  private resize(): void {
    const w = Math.max(1, this.container.clientWidth);
    const h = Math.max(1, this.container.clientHeight);
    this.renderer.setSize(w, h, false);
    this.camera.aspect = w / h;
    this.camera.updateProjectionMatrix();
    this.requestRender();
  }

  requestRender = (): void => {
    if (this.frame || this.disposed) return;
    this.frame = requestAnimationFrame(() => {
      this.frame = 0;
      this.renderer.render(this.scene, this.camera);
    });
  };

  // ---- Camera ---------------------------------------------------------------

  getView(): CameraView {
    const p = this.camera.position, t = this.controls.target;
    return { position: [p.x, p.y, p.z], target: [t.x, t.y, t.z] };
  }

  setView(view: CameraView): void {
    this.camera.position.set(...view.position);
    this.controls.target.set(...view.target);
    this.camera.lookAt(this.controls.target);
    this.controls.update();
    this.requestRender();
  }

  /** Frames all objects (or the default view for an empty scene), keeping the view direction. */
  fitToModel(): void {
    const bounds = unionBounds(this.editor.doc.objects.map(objectBounds));
    if (!bounds) {
      this.setView(DEFAULT_VIEW);
      return;
    }
    const center = new THREE.Vector3(...bounds.min).add(new THREE.Vector3(...bounds.max)).multiplyScalar(0.5);
    const radius = Math.max(new THREE.Vector3(...bounds.max).sub(new THREE.Vector3(...bounds.min)).length() / 2, 0.05);
    const fov = THREE.MathUtils.degToRad(this.camera.fov);
    const fitFov = this.camera.aspect < 1 ? 2 * Math.atan(Math.tan(fov / 2) * this.camera.aspect) : fov;
    const distance = (radius / Math.sin(fitFov / 2)) * 1.1;
    const dir = this.camera.position.clone().sub(this.controls.target).normalize();
    if (dir.lengthSq() === 0) dir.set(0.5, -0.7, 0.5).normalize();
    const pos = center.clone().add(dir.multiplyScalar(distance));
    this.setView({ position: [pos.x, pos.y, pos.z], target: [center.x, center.y, center.z] });
  }

  private onControlsStart = () => {
    this.cameraDrag = true;
  };
  private onControlsEnd = () => {
    this.cameraDrag = false;
  };

  /** Projects a world point to CSS pixel coordinates relative to the canvas. */
  worldToScreen(p: Vec3): [number, number] {
    const v = new THREE.Vector3(...p).project(this.camera);
    const rect = this.renderer.domElement.getBoundingClientRect();
    return [((v.x + 1) / 2) * rect.width, ((1 - v.y) / 2) * rect.height];
  }

  // ---- Input ----------------------------------------------------------------

  private pointerInput(e: PointerEvent): PointerInput {
    const rect = this.renderer.domElement.getBoundingClientRect();
    const sx = e.clientX - rect.left, sy = e.clientY - rect.top;
    const ndc = new THREE.Vector2((sx / rect.width) * 2 - 1, -(sy / rect.height) * 2 + 1);
    this.raycaster.setFromCamera(ndc, this.camera);
    const { origin, direction } = this.raycaster.ray;
    return { ray: { origin: [origin.x, origin.y, origin.z], direction: [direction.x, direction.y, direction.z] }, pick: this.pick(), screen: [sx, sy] };
  }

  /** Raycasts object meshes and resolves the hit triangle to its logical face. */
  private pick(): PointerInput['pick'] {
    const meshes = [...this.views.values()].filter((v) => v.group.visible).map((v) => v.mesh);
    const hit = this.raycaster.intersectObjects(meshes, false)[0];
    if (!hit || hit.faceIndex == null) return null;
    const id = hit.object.userData.objectId as string;
    const view = this.views.get(id);
    const faceId = view?.data.triangleFaces[hit.faceIndex];
    if (!faceId) return null;
    return { objectId: id, faceId, point: [hit.point.x, hit.point.y, hit.point.z] };
  }

  private toolOwnsLeftButton(): boolean {
    return this.editor.getState().tool !== 'orbit';
  }

  private onPointerDown = (e: PointerEvent) => {
    this.renderer.domElement.focus({ preventScroll: true });
    if (e.button !== 0 || !this.toolOwnsLeftButton()) return;
    this.renderer.domElement.setPointerCapture(e.pointerId);
    this.tools.pointerDown(this.pointerInput(e));
  };

  private onPointerMove = (e: PointerEvent) => {
    if (this.cameraDrag || (e.buttons & 6) !== 0) return; // a camera drag owns this movement
    if (!this.toolOwnsLeftButton()) return;
    this.tools.pointerMove(this.pointerInput(e));
  };

  private onPointerUp = (e: PointerEvent) => {
    if (e.button !== 0 || !this.toolOwnsLeftButton()) return;
    if (this.renderer.domElement.hasPointerCapture(e.pointerId)) this.renderer.domElement.releasePointerCapture(e.pointerId);
    this.tools.pointerUp(this.pointerInput(e));
  };

  private onPointerLeave = () => {
    this.editor.setHover(null);
  };

  private onContextMenu = (e: Event) => e.preventDefault();

  // ---- Scene sync -------------------------------------------------------------

  private sync(): void {
    const s = this.editor.getState();
    const prev = this.last;
    this.last = s;
    if (!prev || prev.doc !== s.doc) this.syncObjects(s);
    if (!prev || prev.tool !== s.tool) {
      // Only one consumer per drag: the Orbit tool gives the left button to the camera.
      this.controls.mouseButtons = { LEFT: s.tool === 'orbit' ? THREE.MOUSE.ROTATE : null, MIDDLE: THREE.MOUSE.ROTATE, RIGHT: THREE.MOUSE.PAN };
      this.renderer.domElement.dataset.tool = s.tool;
    }
    if (!prev || prev.fitRequest !== s.fitRequest) {
      if (prev) this.fitToModel();
    }
    if (!prev || prev.doc !== s.doc || prev.selection !== s.selection || prev.hover !== s.hover || prev.preview !== s.preview) {
      this.syncOverlay(s);
    }
    this.requestRender();
  }

  private syncObjects(s: EditorState): void {
    const seen = new Set<string>();
    for (const obj of s.doc.objects) {
      seen.add(obj.id);
      const existing = this.views.get(obj.id);
      if (existing?.source === obj) continue;
      if (existing) {
        this.scene.remove(existing.group);
        disposeObject3D(existing.group);
      }
      this.views.set(obj.id, this.buildObjectView(obj));
    }
    for (const [id, view] of this.views) {
      if (!seen.has(id)) {
        this.scene.remove(view.group);
        disposeObject3D(view.group);
        this.views.delete(id);
      }
    }
  }

  private buildObjectView(obj: ModelObject): ObjectView {
    const data = buildRenderData(obj);
    const group = new THREE.Group();
    group.position.set(...obj.transform.translation);
    group.userData.objectId = obj.id;
    const mesh = new THREE.Mesh(meshGeometry(data), this.materials.face);
    mesh.userData.objectId = obj.id;
    const edges = new THREE.LineSegments(edgeGeometry(data), this.materials.edge);
    group.add(mesh, edges);
    this.scene.add(group);
    group.updateMatrixWorld(true);
    return { source: obj, group, mesh, edges, data };
  }

  private clearOverlay(): void {
    for (const child of [...this.overlay.children]) {
      this.overlay.remove(child);
      disposeObject3D(child);
    }
  }

  private syncOverlay(s: EditorState): void {
    this.clearOverlay();
    for (const v of this.views.values()) {
      v.group.visible = true;
      v.edges.material = s.selection?.objectId === v.source.id ? this.materials.selectedEdge : this.materials.edge;
    }
    const preview = s.preview;
    if (preview?.kind === 'pushpull') {
      const view = this.views.get(preview.objectId);
      const obj = findObject(s.doc, preview.objectId);
      if (view && obj) {
        view.group.visible = false;
        const group = new THREE.Group();
        group.position.set(...obj.transform.translation);
        if (preview.error) {
          const data = buildRenderData(obj, preview.startHeight > 0 ? preview.startHeight : undefined);
          group.add(new THREE.LineSegments(edgeGeometry(data), this.materials.invalidEdge));
          group.add(new THREE.Mesh(meshGeometry(data), this.materials.face));
        } else {
          const data = buildRenderData(obj, preview.height);
          group.add(new THREE.Mesh(meshGeometry(data), this.materials.face));
          group.add(new THREE.LineSegments(edgeGeometry(data), this.materials.previewEdge));
          group.add(new THREE.Mesh(faceGeometry(data, 'cap'), this.materials.selectedFace));
        }
        this.overlay.add(group);
      }
      return;
    }
    if (preview?.kind === 'rectangle') {
      const [x0, y0] = preview.start, [x1, y1] = preview.end;
      const z = preview.start[2] + 1e-5;
      const pts = [x0, y0, z, x1, y0, z, x1, y0, z, x1, y1, z, x1, y1, z, x0, y1, z, x0, y1, z, x0, y0, z];
      const outline = new THREE.LineSegments(new THREE.BufferGeometry().setAttribute('position', new THREE.Float32BufferAttribute(pts, 3)), preview.placement ? this.materials.previewEdge : this.materials.invalidEdge);
      this.overlay.add(outline);
      if (preview.placement) {
        const fill = new THREE.BufferGeometry().setAttribute('position', new THREE.Float32BufferAttribute([x0, y0, z, x1, y0, z, x1, y1, z, x0, y0, z, x1, y1, z, x0, y1, z], 3));
        this.overlay.add(new THREE.Mesh(fill, this.materials.previewFill));
      }
      return;
    }
    // Selection and hover highlights resolve logical faces to all of their triangles.
    const sel = s.selection;
    if (sel?.faceId) this.addFaceOverlay(sel.objectId, sel.faceId, this.materials.selectedFace);
    const hover = s.hover;
    if (hover && !(sel && sel.objectId === hover.objectId && sel.faceId === hover.faceId)) {
      this.addFaceOverlay(hover.objectId, hover.faceId, this.materials.hoverFace);
    }
  }

  private addFaceOverlay(objectId: string, faceId: FaceId, material: THREE.Material): void {
    const view = this.views.get(objectId);
    if (!view || !view.data.faces.includes(faceId)) return;
    const mesh = new THREE.Mesh(faceGeometry(view.data, faceId), material);
    mesh.position.copy(view.group.position);
    this.overlay.add(mesh);
  }
}

function meshGeometry(data: RenderData): THREE.BufferGeometry {
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.BufferAttribute(data.positions, 3));
  g.setAttribute('normal', new THREE.BufferAttribute(data.normals, 3));
  g.setIndex(new THREE.BufferAttribute(data.indices, 1));
  g.computeBoundingSphere();
  return g;
}

function edgeGeometry(data: RenderData): THREE.BufferGeometry {
  return new THREE.BufferGeometry().setAttribute('position', new THREE.BufferAttribute(data.edges, 3));
}

/** Geometry containing every triangle of one logical face. */
function faceGeometry(data: RenderData, faceId: FaceId): THREE.BufferGeometry {
  const idx: number[] = [];
  data.triangleFaces.forEach((f, t) => {
    if (f === faceId) idx.push(data.indices[3 * t]!, data.indices[3 * t + 1]!, data.indices[3 * t + 2]!);
  });
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.BufferAttribute(data.positions, 3));
  g.setIndex(idx);
  return g;
}
