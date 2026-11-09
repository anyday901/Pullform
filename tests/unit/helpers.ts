import type { Ray, Vec3 } from '../../src/core/vec';
import { normalize, sub } from '../../src/core/vec';
import { Editor } from '../../src/editor/Editor';
import { ToolController, type PointerInput } from '../../src/tools/ToolController';

/** A ray looking straight down at (x, y). */
export function downRay(x: number, y: number): Ray {
  return { origin: [x, y, 10], direction: [0, 0, -1] };
}

/** A ray from `from` toward `to`. */
export function rayTo(from: Vec3, to: Vec3): Ray {
  return { origin: from, direction: normalize(sub(to, from)) };
}

export function input(ray: Ray, screen: [number, number] = [0, 0], pick: PointerInput['pick'] = null): PointerInput {
  return { ray, pick, screen };
}

export function setup() {
  const editor = new Editor();
  const tools = new ToolController(editor);
  return { editor, tools };
}

export const sideViewRay = (height: number, x = 1, y = 0.5): Ray =>
  rayTo([x, y - 10, height], [x, y, height]);
