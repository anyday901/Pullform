import { expect, test, type Page } from '@playwright/test';
import { readFile } from 'node:fs/promises';

type Doc = {
  nextId: number;
  objects: { id: string; profile: { boundary: [number, number][] }; extrusion: { height: number } | null; transform: { translation: number[] } }[];
};

const doc = (page: Page) => page.evaluate(() => window.__modeler!.editor.doc) as Promise<Doc>;
const history = (page: Page) =>
  page.evaluate(() => ({ undo: window.__modeler!.editor.undoDepth, redo: window.__modeler!.editor.redoDepth }));
const preview = (page: Page) => page.evaluate(() => window.__modeler!.editor.getState().preview);
const mod = process.platform === 'darwin' ? 'Meta' : 'Control';

/** Page coordinates of a world point. */
async function at(page: Page, p: [number, number, number]) {
  const box = (await page.locator('canvas').boundingBox())!;
  const [x, y] = await page.evaluate((p) => window.__modeler!.viewport!.worldToScreen(p), p);
  return { x: box.x + x, y: box.y + y };
}

async function clickWorld(page: Page, p: [number, number, number]) {
  const { x, y } = await at(page, p);
  await page.mouse.move(x, y, { steps: 3 });
  await page.mouse.click(x, y);
}

test.beforeEach(async ({ page }) => {
  const errors: string[] = [];
  page.on('pageerror', (e) => errors.push(String(e)));
  page.on('console', (m) => m.type() === 'error' && errors.push(m.text()));
  // The production build must work offline: every request stays on the local server.
  page.on('request', (r) => {
    const url = new URL(r.url());
    if (!['localhost', '127.0.0.1'].includes(url.hostname) && !['blob:', 'data:'].includes(url.protocol)) errors.push(`external request: ${r.url()}`);
  });
  await page.goto('/');
  await expect(page.locator('canvas')).toBeVisible();
  await page.waitForFunction(() => !!window.__modeler?.viewport);
  (page as unknown as { errors: string[] }).errors = errors;
});

test.afterEach(async ({ page }) => {
  expect((page as unknown as { errors: string[] }).errors).toEqual([]);
});

test('full modeling workflow through the interface', async ({ page }) => {
  // Empty scene, toolbar and status hints are visible.
  await expect(page.getByRole('toolbar', { name: 'Tools' })).toBeVisible();
  await expect(page.getByTestId('status-hint')).toContainText('Select');
  expect((await doc(page)).objects).toHaveLength(0);
  await expect(page.locator('canvas')).toHaveCount(1);

  // Draw a 2000 x 1000 mm rectangle with two clicks (grid snap on).
  await page.keyboard.press('r');
  await expect(page.getByTestId('status-hint')).toContainText('first corner');
  await clickWorld(page, [0, 0, 0]);
  await clickWorld(page, [2, 1, 0]);
  let d = await doc(page);
  expect(d.objects).toHaveLength(1);
  expect(d.objects[0]!.profile.boundary[2]![0]).toBeCloseTo(2, 9);
  expect(d.objects[0]!.profile.boundary[2]![1]).toBeCloseTo(1, 9);
  const id = d.objects[0]!.id;
  expect(await history(page)).toEqual({ undo: 1, redo: 0 });

  // Push/pull: click the face, then type an exact distance (digits go to the measurements box).
  await page.keyboard.press('p');
  await clickWorld(page, [1, 0.5, 0]);
  expect((await preview(page))?.kind).toBe('pushpull');
  await page.keyboard.type('750');
  await expect(page.getByTestId('measure-distance')).toHaveValue('750');
  await page.keyboard.press('Enter');
  d = await doc(page);
  expect(d.objects[0]!.extrusion!.height).toBeCloseTo(0.75, 12);
  expect(await history(page)).toEqual({ undo: 2, redo: 0 });

  // Select the cap with the mouse and set the total height explicitly.
  await page.keyboard.press('Space');
  await clickWorld(page, [1, 0.5, 0.75]);
  await expect(page.getByTestId('prop-face')).toHaveText('Cap (top)');
  await expect(page.getByTestId('prop-id')).toHaveText(id);
  const height = page.getByTestId('prop-height');
  await height.click();
  await height.fill('900mm');
  // Shortcut letters typed in a text field must not switch tools.
  await expect(page.locator('[data-tool-id="select"]')).toHaveAttribute('aria-pressed', 'true');
  await height.press('Enter');
  d = await doc(page);
  expect(d.objects).toHaveLength(1);
  expect(d.objects[0]!.id).toBe(id);
  expect(d.objects[0]!.extrusion!.height).toBeCloseTo(0.9, 12);

  // The cap is still pickable at its new height.
  await page.locator('canvas').focus();
  await clickWorld(page, [-1.5, 2.5, 0]);
  await expect(page.getByTestId('selection-properties')).toHaveCount(0);
  await clickWorld(page, [1, 0.5, 0.9]);
  await expect(page.getByTestId('prop-face')).toHaveText('Cap (top)');

  // Undo / redo with keyboard.
  await page.keyboard.press(`${mod}+z`);
  expect((await doc(page)).objects[0]!.extrusion!.height).toBeCloseTo(0.75, 12);
  await page.keyboard.press(`${mod}+z`);
  expect((await doc(page)).objects[0]!.extrusion).toBeNull();
  await page.keyboard.press(`${mod}+Shift+z`);
  await page.keyboard.press(`${mod}+Shift+z`);
  expect((await doc(page)).objects[0]!.extrusion!.height).toBeCloseTo(0.9, 12);
  expect(await history(page)).toEqual({ undo: 3, redo: 0 });

  // Mouse-driven push/pull of the cap (preview, then click) creates exactly one entry.
  await page.keyboard.press('p');
  await clickWorld(page, [1, 0.5, 0.9]);
  const target = await at(page, [1, 0.5, 1.4]);
  await page.mouse.move(target.x, target.y, { steps: 10 });
  expect(await history(page)).toEqual({ undo: 3, redo: 0 });
  await page.mouse.click(target.x, target.y);
  const h = (await doc(page)).objects[0]!.extrusion!.height;
  expect(h).toBeGreaterThan(1.0);
  expect(await history(page)).toEqual({ undo: 4, redo: 0 });
  await page.keyboard.press(`${mod}+z`);
  expect((await doc(page)).objects[0]!.extrusion!.height).toBeCloseTo(0.9, 12);
});

test('previews cancel cleanly and camera navigation does not edit the model', async ({ page }) => {
  await page.keyboard.press('r');
  await clickWorld(page, [0, 0, 0]);
  await clickWorld(page, [1, 1, 0]);
  const before = await doc(page);

  // Rectangle preview + Escape.
  await clickWorld(page, [2, 2, 0]);
  const p = await at(page, [3, 3, 0]);
  await page.mouse.move(p.x, p.y, { steps: 5 });
  expect((await preview(page))?.kind).toBe('rectangle');
  await page.keyboard.press('Escape');
  expect(await preview(page)).toBeNull();
  expect(await doc(page)).toEqual(before);
  expect(await history(page)).toEqual({ undo: 1, redo: 0 });

  // Push/pull preview + Escape.
  await page.keyboard.press('p');
  await clickWorld(page, [0.5, 0.5, 0]);
  const up = await at(page, [0.5, 0.5, 0.6]);
  await page.mouse.move(up.x, up.y, { steps: 5 });
  expect((await preview(page))?.kind).toBe('pushpull');
  await page.keyboard.press('Escape');
  expect(await preview(page)).toBeNull();
  expect(await doc(page)).toEqual(before);
  expect(await history(page)).toEqual({ undo: 1, redo: 0 });

  // With the rectangle tool active: middle-drag orbits, right-drag pans, wheel zooms.
  await page.keyboard.press('r');
  const cam = () => page.evaluate(() => window.__modeler!.viewport!.getView());
  const box = (await page.locator('canvas').boundingBox())!;
  const cx = box.x + box.width / 2, cy = box.y + box.height / 2;
  let view = await cam();
  for (const button of ['middle', 'right'] as const) {
    await page.mouse.move(cx, cy);
    await page.mouse.down({ button });
    await page.mouse.move(cx + 120, cy + 40, { steps: 8 });
    await page.mouse.up({ button });
    const next = await cam();
    expect(next).not.toEqual(view);
    view = next;
  }
  await page.mouse.wheel(0, -400);
  await page.waitForTimeout(100);
  expect(await cam()).not.toEqual(view);
  expect(await preview(page)).toBeNull();
  expect(await doc(page)).toEqual(before);
  expect(await history(page)).toEqual({ undo: 1, redo: 0 });

  // Orbit tool: left drag moves the camera, not the model.
  await page.getByRole('button', { name: /^Orbit/ }).click();
  view = await cam();
  await page.mouse.move(cx, cy);
  await page.mouse.down();
  await page.mouse.move(cx - 100, cy, { steps: 8 });
  await page.mouse.up();
  expect(await cam()).not.toEqual(view);
  expect(await doc(page)).toEqual(before);

  // Fit to model frames the object.
  await page.keyboard.press('f');
  const c = await at(page, [0.5, 0.5, 0]);
  expect(Math.abs(c.x - cx)).toBeLessThan(40);
  expect(Math.abs(c.y - cy)).toBeLessThan(40);
});

test('exact entry, delete with undo, save, reopen and continue editing', async ({ page }) => {
  // Exact rectangle entry using the measurement fields.
  await page.keyboard.press('r');
  await page.getByTestId('measure-width').fill('2m');
  await page.getByTestId('measure-depth').fill('100cm');
  await page.getByTestId('measure-depth').press('Enter');
  // Unsupported input produces a message and no edit.
  await page.getByTestId('measure-width').fill('6\'2"');
  await page.getByTestId('measure-depth').fill('100');
  await page.getByTestId('measure-depth').press('Enter');
  await expect(page.locator('.measurements .field-error')).toHaveCount(1);
  await expect(page.locator('.measurements .field-error')).toContainText('feet-and-inches');
  // A second object from mouse input, overlapping the first.
  await page.keyboard.press('Escape');
  await page.locator('canvas').focus();
  await clickWorld(page, [1.5, 0.5, 0]);
  await clickWorld(page, [2.5, 1.5, 0]);
  let d = await doc(page);
  expect(d.objects.map((o) => o.id)).toEqual(['obj-1', 'obj-2']);

  // Select and delete obj-2, then undo.
  await page.keyboard.press('Space');
  await clickWorld(page, [2.3, 1.3, 0]);
  await expect(page.getByTestId('prop-id')).toHaveText('obj-2');
  await page.keyboard.press('Delete');
  expect((await doc(page)).objects.map((o) => o.id)).toEqual(['obj-1']);
  await expect(page.getByTestId('selection-properties')).toHaveCount(0);
  await page.keyboard.press(`${mod}+z`);
  expect((await doc(page)).objects.map((o) => o.id)).toEqual(['obj-1', 'obj-2']);
  await expect(page.getByTestId('prop-id')).toHaveText('obj-2');

  // Extrude obj-1 via the properties panel.
  await clickWorld(page, [0.3, 0.3, 0]);
  await expect(page.getByTestId('prop-id')).toHaveText('obj-1');
  await page.getByTestId('prop-height').fill('750');
  await page.getByTestId('prop-height').press('Enter');
  d = await doc(page);

  // Save downloads a versioned JSON project.
  const [download] = await Promise.all([page.waitForEvent('download'), page.getByRole('button', { name: 'Save project' }).click()]);
  expect(download.suggestedFilename()).toBe('untitled.pullform.json');
  const path = await download.path();
  const saved = JSON.parse(await readFile(path, 'utf8'));
  expect(saved.format).toBe('pullform-project');
  expect(saved.version).toBe(1);
  expect(saved.document).toEqual(d);

  // New (no prompt because saved), then reopen.
  await page.getByRole('button', { name: 'New project' }).click();
  expect((await doc(page)).objects).toHaveLength(0);
  await page.getByTestId('open-file-input').setInputFiles({ name: 'table.pullform.json', mimeType: 'application/json', buffer: Buffer.from(JSON.stringify(saved)) });
  await expect(page.getByTestId('status-message')).toContainText('Opened table.pullform.json');
  expect(await doc(page)).toEqual(d);
  expect(await history(page)).toEqual({ undo: 0, redo: 0 });

  // Continue editing after reopening.
  await page.locator('canvas').focus();
  await clickWorld(page, [0.3, 0.3, 0.75]);
  await expect(page.getByTestId('prop-face')).toHaveText('Cap (top)');
  await page.getByTestId('prop-height').fill('0.9m');
  await page.getByTestId('prop-height').press('Enter');
  expect((await doc(page)).objects[0]!.extrusion!.height).toBeCloseTo(0.9, 12);
  await page.locator('canvas').focus();
  await page.keyboard.press('r');
  await page.getByTestId('measure-width').fill('100, 100');
  await page.getByTestId('measure-width').press('Enter');
  expect((await doc(page)).objects.at(-1)!.id).toBe('obj-3');

  // Unsaved changes: New asks first; dismissing keeps the model.
  page.once('dialog', (dialog) => dialog.dismiss());
  await page.getByRole('button', { name: 'New project' }).click();
  expect((await doc(page)).objects).toHaveLength(3);

  // Invalid and future-version files leave the open model intact.
  const current = await doc(page);
  await page.evaluate(() => (window.__modeler!.actions.confirmDiscard = () => true));
  await page.getByTestId('open-file-input').setInputFiles({ name: 'bad.json', mimeType: 'application/json', buffer: Buffer.from('{not json') });
  await expect(page.getByTestId('status-message')).toContainText('not valid JSON');
  await page.getByTestId('open-file-input').setInputFiles({ name: 'future.json', mimeType: 'application/json', buffer: Buffer.from(JSON.stringify({ ...saved, version: 2 })) });
  await expect(page.getByTestId('status-message')).toContainText('newer');
  expect(await doc(page)).toEqual(current);
  expect((await history(page)).undo).toBeGreaterThan(0);
});
