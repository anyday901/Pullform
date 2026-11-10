# Pullform

A free, open-source 3D modeler for dimensioned geometry: buildings, rooms, furniture,
woodworking and other objects. It follows an approachable "draw a shape, push/pull it into
a solid" workflow. It runs locally in a desktop browser on Windows, Linux and macOS, with no
account, backend, or network connection required.

Pullform is named for its core move: draw a profile, then pull it into form. The
project is not affiliated with SketchUp and does not read or write `.skp` files.

## What works (milestone 1)

1. An empty scene with a ground grid (100 mm minor, 1 m major) and red/green/blue X/Y/Z axes.
2. Orbit, pan, zoom (toward the cursor) and **Fit** the camera to the model.
3. Draw a rectangle on the ground plane with the mouse (click-click or drag) in any
   direction, or type an exact width and depth. Optional grid snap with a visible state.
4. Push/pull a rectangle into a closed rectangular prism by pointer or exact distance.
5. Select an extrusion's top cap and change its height (drag, a relative distance, or an
   absolute **Total height** in the properties panel). The same object is updated.
6. Select and delete a whole object.
7. Undo and redo every committed modeling operation.
8. Save an editable, versioned JSON project (as a download), reopen it, and keep editing.

### Current limits

- Shapes are rectangles on the ground plane (z = 0) and their extrusions only.
- Push/pull works on a planar rectangle and on an extrusion's top cap. Side faces and
  base faces cannot be pushed yet. Heights must be positive; pushing below the base,
  holes, cutting through solids and merging touching geometry are not supported.
- Objects stay independent when they touch or overlap.
- No move/rotate/scale tools yet; objects keep the position where they were drawn.

These limits are also listed in the app's properties panel. See
[docs/ROADMAP.md](docs/ROADMAP.md) for what comes next.

## Prerequisites

- Node.js 22.12 or newer (developed with Node 22.23) and npm.
- A desktop browser with WebGL: current Chrome/Chromium, Edge, Firefox or Safari.

## Install, run, test, build

```sh
git clone https://github.com/anyday901/pullform.git
cd pullform
npm ci                 # install the exact locked dependencies
npm run dev            # start the dev server at http://localhost:5173
npm test               # unit tests (Vitest)
npm run typecheck      # TypeScript checks for app and tests
npm run build          # typecheck + production build into dist/
npm run preview        # serve the production build at http://localhost:4173
npm run test:e2e       # browser tests (Playwright; builds and serves the app itself)
npm run check          # typecheck + unit tests + build
```

The browser tests use Playwright's Chromium. If it is not installed on your machine, run
`npx playwright install chromium` once. To also run them in Firefox:
`npx playwright install firefox` and then `E2E_FIREFOX=1 npm run test:e2e`.

The production build in `dist/` is fully self-contained (relative paths, no CDN, fonts or
remote assets), so any static file server works, e.g. `npx vite preview` or
`python3 -m http.server -d dist`. Opening `dist/index.html` directly from disk is not
supported because browsers block ES module loading from `file://` URLs.

## Controls

| Action | Mouse / key |
| --- | --- |
| Use the active tool | Left button |
| Orbit | Middle drag, or the Orbit tool (O) with left drag |
| Pan | Right drag, or Shift + middle drag |
| Zoom | Mouse wheel (zooms toward the cursor) |
| Select tool | Space |
| Rectangle tool | R |
| Push/Pull tool | P |
| Orbit tool | O |
| Fit view to model | F |
| Toggle grid snap | G |
| Cancel the current operation, or clear the selection | Esc |
| Delete the selected object | Delete or Backspace |
| Undo | Ctrl+Z (⌘Z on macOS) |
| Redo | Ctrl+Y or Ctrl+Shift+Z (⌘⇧Z on macOS) |
| Open / Save project | Ctrl+O / Ctrl+S (⌘O / ⌘S) |

Shortcuts are ignored while you type in a text field. While drawing or push/pulling, typing
a digit jumps into the Measurements box; press Enter to apply.

### Drawing a rectangle

Press **R**, click the first corner on the ground, then click the opposite corner (or press,
drag and release). To enter an exact size, type the width and depth into the Measurements
box (`2000` and `1000`, or `2000, 1000` in the width field) and press Enter. If a first
corner is placed, the typed rectangle is drawn from it toward the pointer; otherwise it
starts at the origin.

### Push/pull

Press **P**, click a rectangle (or an extrusion's top cap), move the pointer up and click
again, or type a distance and press Enter. The **Distance (Δ)** box is relative to the
face's current position; **Total height** in the properties panel is absolute, measured
from the base.

### Lengths and units

The model is stored in meters. Lengths are displayed in millimeters by default (the unit
selector in the top bar changes the display unit, which is saved with the project).
Inputs accept `mm`, `cm`, `m`, `in` (or `"`) and `ft` (or `'`) suffixes, e.g. `2000`,
`2000mm`, `200 cm`, `2m`, `78.74in`, `6.5ft`. **A value without a unit is interpreted in
the current display unit** (millimeters by default). Use `.` as the decimal separator.
Compound feet-and-inches (`6'2"`) and expressions are not supported yet and are rejected
with a message.

### Projects

- **Save** downloads `<name>.pullform.json` (works in every browser, including Firefox).
- **Open** reads a project file. The whole file is validated first; a malformed file or an
  unsupported version shows an error and leaves the current model untouched.
- **New** and **Open** ask before discarding unsaved changes, and the browser warns when
  you close a tab with unsaved changes.
- Opening or starting a project clears the undo history: undo never crosses a project
  boundary.

## Documentation

- [Architecture](docs/ARCHITECTURE.md): document model, render mapping, units, history,
  persistence and geometry boundaries.
- [Roadmap](docs/ROADMAP.md): completed and planned work.
- [Verification](docs/VERIFICATION.md): what has been tested, how, and what has not.
- [Changelog](CHANGELOG.md): what changed in each release.
- [Contributing](CONTRIBUTING.md): setup and pull request checklist.

## License

MIT for this project's code; see [LICENSE](LICENSE). Bundled dependencies keep their own
licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
