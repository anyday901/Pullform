# Verification

Status as of milestone 1 (version 0.1.0). Environment used: Linux container, Node 22.23,
headless Chromium (Playwright 1.63, build 1243) with software WebGL (SwiftShader).

## Automated

| Check | Command | Result |
| --- | --- | --- |
| TypeScript (app + tests) | `npm run typecheck` | pass |
| Unit tests (Vitest, 33 tests) | `npm test` | pass |
| Production build | `npm run build` | pass |
| Browser tests (Playwright, Chromium, 4 tests) | `npm run test:e2e` | pass |

Unit tests (`tests/unit/`) cover, against the required scenarios:

1. 2000 × 1000 mm rectangle extruded to 750 mm has model-space bounds 2 × 1 × 0.75 m.
2. The prism has six logical faces, 12 boundary edges (no diagonals), is closed and
   consistently outward-wound (`checkSolid`, signed volume 1.5 m³); a flipped triangle is
   detected. Real Three.js raycasts on either side of the cap diagonal hit different
   triangles that both resolve to `cap`; a ray from inside the solid hits nothing
   (faces are single-sided and point outward).
3. Height 750 → 900 mm updates the same object ID; the cap remains selectable and
   push/pullable.
4. Undo 900 → 750 → planar rectangle; redo restores both. Selection follows (cap ↔ face).
5. Escape during rectangle and push/pull previews leaves the document object identical and
   history depth unchanged; many preview moves add no history entries.
6. Two overlapping objects stay independent; deleting one and undoing restores IDs and
   selection; deleted IDs are never reused; a new edit clears redo; history limit is enforced.
   The display unit survives undo/redo, and unsaved-change tracking accounts for it.
7. Save → parse → reload yields an identical document (geometry, units, IDs, nextId,
   camera); editing continues after reopening with the next ID `obj-3`; undoing back to the
   opened state clears the dirty flag.
8. `2000`, `2000mm`, `200cm`, `2m` parse identically; inches/feet; unitless uses display unit.
9. Rejections: malformed lengths, `Infinity`/`NaN`/`1e999`, comma decimals, compound
   feet-inches, degenerate/zero/negative rectangles and heights, and 13 kinds of invalid
   project files (bad JSON, wrong format, future/missing version, bad unit, duplicate IDs,
   low nextId, degenerate/clockwise boundary, null coordinates, negative height, bad ID).

Browser tests (`tests/e2e/workflow.spec.ts`) drive the built app through the real UI
with mouse and keyboard:

- Draw a rectangle by clicking two corners; push/pull by clicking the face and typing
  `750`; select the cap by clicking it; type `900mm` into Total height (letters typed in the
  field do not trigger tool shortcuts); click empty space to clear selection; re-pick the cap
  at its new height; Ctrl+Z/Ctrl+Shift+Z; a mouse-driven cap push/pull adds exactly one undo
  entry.
- Escape cancels rectangle and push/pull previews with no document or history change.
  With the Rectangle tool active, middle-drag orbits, right-drag pans and the wheel zooms
  without creating previews or edits; the Orbit tool's left drag moves only the camera;
  Fit centers the model.
- Exact entry via the Width/Depth fields; an unsupported `6'2"` shows a message; select
  and Delete an overlapping object, undo restores it with the same ID; Save produces a
  `pullform-project` v1 download equal to the in-memory document; New; Open via the file
  input restores the identical document and empty history; editing continues; New with
  unsaved changes prompts (dismiss keeps the model); invalid JSON and a future version
  show errors and leave the model and history intact.
- Typing a width, Enter, a depth, Enter creates the typed rectangle (with and without a
  first corner placed) and leaves no stale text; Escape in Total height discards the typed
  value while clicking away applies it; undo keeps the chosen display unit.
- Every test asserts no page errors, no console errors, a single canvas, and **no request
  to any non-local host** (the production build works offline).

## Manual (performed by inspecting screenshots from scripted Chromium sessions)

- Layout, grid, axes, edges, selection and hover colors, push/pull preview, and the
  measurement/status areas render as intended at 1400 × 850.
- Dev server (`npm run dev`, React StrictMode double-mount) shows exactly one canvas and no
  console errors, confirming viewport cleanup on unmount.

## Performance baseline (measured)

400 extruded boxes, Select tool, 1400 × 850 viewport, headless Chromium with software
WebGL: pointer-move handling (raycast + face hover resolution) averaged **0.08 ms per
event** over 200 events; a full frame render averaged **2.1 ms**. GPU hardware will differ;
treat this as an order-of-magnitude baseline only.

## Not verified

- **Firefox and Safari/WebKit were not exercised.** Installing Playwright's Firefox was
  blocked by this environment's network policy (`cdn.playwright.dev` and
  `playwright.download.prss.microsoft.com` are not allowlisted). To verify Firefox:
  `npx playwright install firefox && E2E_FIREFOX=1 npm run test:e2e`.
- Real-hardware GPUs, trackpads, high-DPI displays and macOS ⌘ shortcuts were not tested
  by hand. Windows and macOS were not run.
- The native file-picker dialog and the browser's save-download location were exercised
  only through Playwright's file input and download APIs, not by hand.
- The "unsaved changes" warning on closing the tab (`beforeunload`) is implemented but not
  covered by an automated test.
