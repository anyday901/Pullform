# Roadmap

Priorities are listed top to bottom. Each item notes its dependencies and a small
acceptance check.

## Completed: Milestone 1, the first modeling workflow

- [x] Empty scene with grid and axes; orbit, pan, zoom, fit to model.
- [x] Rectangle on the ground plane by mouse (any direction) or exact width/depth; grid snap.
- [x] Push/pull a rectangle into a closed prism by pointer or exact distance.
- [x] Select the cap and change total height; the same object is updated.
- [x] Select and delete whole objects.
- [x] Undo/redo through one commit path; previews never enter history; Escape cancels.
- [x] Versioned JSON save and open via native dialogs, atomic writes, full validation and
      unsaved-changes prompts (New, Open, quit, window close).
- [x] Units: meters internally, millimeters by default, mm/cm/m/in/ft input.
- [x] Native C++20 application (GLFW, OpenGL 3.3 core, Dear ImGui) with a headless core.
- [x] Windows NSIS installer; Linux and macOS source builds; CI on all three plus ASan/UBSan.
- [x] Headless doctest suite for geometry, units, history, tools, camera and files.

## Next: Milestone 2, move and precision basics (recommended next)

1. **Move tool** with exact distance and axis locking. *Depends on:* nothing new.
   *Accept:* move an object 500 mm along X by typing; undo restores the old position;
   one undo entry per move.
2. **Endpoint/midpoint snapping and inference guides** for rectangle corners and push/pull
   heights. *Depends on:* a snap-candidate index built from `buildFaces`.
   *Accept:* the second rectangle snaps exactly to a corner of the first; a height snaps to
   another object's cap.
3. **Duplicate** (copy) objects. *Depends on:* Move. *Accept:* the copy has a new stable ID
   and editing it leaves the original unchanged.
4. **Tape measure and dimension annotations.** *Depends on:* snapping.
   *Accept:* the measured distance between two corners matches the model exactly.
5. **Rotate and scale** (done: Rotate and Scale tools, file format version 2) with typed angles/factors; full transforms in the document
   (schema version 2 with a migration from version 1). *Accept:* a rotated object saves,
   reopens and picks correctly.

## Modeling tools

- Lines and closed polygon profiles (`profile.kind: 'polygon'`, ear-clipping
  triangulation, self-intersection checks). *Accept:* an L-shaped profile extrudes to a
  closed solid that passes `checkSolid`.
- Circles and arcs (stored as true curves, tessellated for display).
- Drawing on other planes: on existing faces and on vertical planes (uses the `plane`
  basis already in the document).

## General face editing (needs a topology decision first)

- Evaluate a topology model (half-edge B-rep) versus a geometry kernel (e.g. OpenCascade
  or Manifold, both usable from C++) before promising these. *Accept:* a written decision with
  size, license and performance findings.
- Push/pull of side faces and arbitrary planar faces; negative push/pull (cutting).
- Splitting faces with lines, intersections, connected geometry, offsets, openings/holes.

## Organization

- Groups and nested editing contexts; component definitions and instances.
- Outliner panel; visibility tags/layers.

## Presentation

- Materials and colors; standard views (top/front/side/iso); orthographic camera; saved
  scenes.

## Interchange

- Export glTF/GLB and OBJ; STL for 3D printing with unit selection and a manifold check
  (`checkSolid` already verifies closed, oriented solids).
- Import of meshes as non-editable reference geometry, clearly distinct from native
  editable profiles.
- Proprietary formats such as `.skp` are a separate research task with no promise of
  support.

## Reliability

- Autosave to a recovery file beside the user's config directory, and crash recovery.
- Larger-model performance: instancing/merged buffers, BVH picking. Baseline measured for
  milestone 1: see docs/VERIFICATION.md.
- File format migrations with tests for every previous version.
- Automated viewport tests: render known scenes offscreen (Mesa) and compare images, so
  rendering regressions are caught in CI and not only by manual runs.

## Distribution

- Code-signed Windows installer and a winget manifest; a signed and notarized macOS app
  bundle (`.dmg`); a Linux AppImage or Flatpak. *Depends on:* signing certificates.
  *Accept:* the installer runs without SmartScreen/Gatekeeper warnings.
- Optional Vulkan or Metal renderer behind the existing `Renderer` interface.
