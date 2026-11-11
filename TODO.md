# TODO

The plan for taking Pullform from "rectangles you can push and pull" to a modeler that can
stand next to SketchUp for everyday work: buildings, rooms, furniture and woodworking.

Each numbered item is sized to be one pull request (a few are marked as two). Items name
what they depend on and end with an **Accept** check, so they can be handed out on their
own. Items with no unfinished dependency can be worked on in parallel; the
[Start now](#start-now) list names them. Item numbers are roadmap numbers, not GitHub PR
numbers.

## Where Pullform stands (November 2026)

Merged into `main`:

- [x] First modeling workflow: grid and axes, orbit/pan/zoom/fit, rectangle on the ground,
      push/pull into a prism, Total height on the cap, select and delete, undo/redo,
      versioned JSON projects, units, Windows installer, CI on three platforms (#1).
- [x] Input fixes: Backspace guard, Total height commit, exact typed sizes, far first
      corner refused (#2).
- [x] File safety: durable atomic saves, Save As replace check, Unicode command line (#3).
- [x] UI scale reset between monitors, tooltips and caret while idle (#4).
- [x] Hover picking made linear, 5,000-box benchmark (#5).
- [x] Move (M) with typed distance, vector and axis locks; tap Ctrl to copy (#6).
- [x] Endpoint and midpoint snapping with X/Y/Z inference guides and height locks (#8).
- [x] Tape Measure (T) and attached Dimension annotations (D) (#9).
- [x] Rotate (Q) and Scale (S), file format version 2 with migration from version 1 (#10).
- [x] Duplicate (Ctrl+D) with a new stable ID (#11).
- [x] Phase 1 foundations, items 1 to 5: per-tool classes, multi-selection, the geometry
      decision in `docs/GEOMETRY.md`, the edge/face mesh with file format version 3, and
      edge/face rendering and picking (#13).
- [x] Phase 2 drawing, items 6 to 13: Line tool with push/pull of drawn faces (#18), the
      full inference engine (#20), drawing on any face and plane (#19), Circle and Polygon
      (#21), Arc, 3 Point Arc and Pie (#17), Eraser (#16), construction guides and the
      Protractor (#22), and Freehand (#15).

What SketchUp has that Pullform does not, in short: push/pull on any face (including
cutting holes), Offset and Follow Me, groups and components, materials, styles, scenes,
shadows, section planes, and import/export. Drawing is in place (lines, curves, guides,
any face), so the next gap is editing that geometry, starting with item 14.

## Open items carried over

These are still open from the first batch.

- [ ] **0a. Manual checks on real Windows 10/11.** Run the installer and the app; drag the
      window between monitors with different scaling (UI size must stay right, not grow);
      tooltips appear with the mouse still and the caret blinks; Save As onto an existing
      project shows the Replace prompt and "No" leaves the file untouched; the native
      dialogs work. Record results in `docs/VERIFICATION.md`. *Needs:* a Windows machine.
- [ ] **0b. Run the macOS build on a Mac**, including Retina scaling, and record it.
- [ ] **0c. File association in the installer.** Register `.pullform.json` with an icon and
      "Open with Pullform" (the packaging has none yet). *Accept:* double-clicking a project
      opens it, including one with a non-English name (the wide command line from #3 is
      in place).
- [ ] **0d. Release v0.2.0.** Tag it and attach the installer to a GitHub release. Fold in
      the CHANGELOG (its Unreleased section has two `### Added` headings to merge).

## Phase 1: Foundations (done)

- [x] **1. Split `ToolController` into per-tool classes.** `tool_controller.cpp` is about
      950 lines with every tool's state machine in one class. Give each tool its own class
      behind a small interface (pointer down/move/up, key, typed value, preview, cancel,
      status hint), keep shared parts (snapping, numeric entry, axis locks) in helpers.
      Behavior must not change. *Depends on:* nothing. *Accept:* all existing tests pass
      unchanged; adding a tool touches one new file pair plus a registration line.
- [x] **2. Multi-selection of objects.** Shift adds/removes, Ctrl adds, click empty space
      clears, Ctrl+A selects all; left-to-right window selection and right-to-left crossing
      selection. Move, Rotate, Scale, Duplicate and Delete act on the whole selection as one
      undo entry. *Depends on:* nothing (works on today's objects). *Accept:* select three
      boxes by window, move them 1 m with one undo entry.
- [x] **3. Geometry model decision** (a written document, no product code). SketchUp's feel
      comes from "sticky" geometry: edges and faces that share vertices, faces that form
      automatically when edges close a loop, and edges that split faces. Compare a
      half-edge planar mesh of our own against building on a kernel (Manifold for booleans,
      OpenCascade for B-rep). Cover: how today's profile objects migrate (proposal: each
      becomes a group holding its own mesh, so nothing merges unexpectedly), curves stored
      as true arcs plus segments, precision and tolerance merging, undo snapshot cost,
      license and binary size. *Depends on:* nothing. *Accept:* `docs/GEOMETRY.md`
      reviewed and merged with a clear recommendation.
- [x] **4. Edge/face mesh in the core, file format version 3** (likely two PRs: data model,
      then migration). Vertices, edges and faces with holes; automatic face creation from a
      closed planar loop; splitting a face by an edge across it; merging coplanar faces when
      the edge between them is erased; validation. Version 2 files open with a migration.
      *Depends on:* 3. *Accept:* four edges drawn in a loop make one face; a line across it
      splits it in two; every version 1 and 2 test file opens and renders the same.
- [x] **5. Render and pick edges and faces.** Draw edges as lines (with a thicker
      "profile" outline option), pick vertex/edge/face under the pointer, highlight the
      hovered element, select edges and faces as well as objects (click, double-click for a
      face plus its edges, triple-click for everything connected). *Depends on:* 4.
      *Accept:* click an edge, then a face, then triple-click; each highlights correctly.

## Phase 2: Drawing (done)

- [x] **6. Line tool (L).** Click-click chains of edges, typed length, close a loop to make
      a face, Escape ends the chain. *Depends on:* 4, 5. *Accept:* draw an L-shaped floor
      plan by typing lengths; it becomes one face that push/pull extrudes.
- [x] **7. Full inference engine.** Add to today's endpoint/midpoint/axis inference: on
      edge, on face, intersection, center of arc, parallel and perpendicular to an edge,
      from a point along an axis, "magenta" parallel guides, Shift to lock the current
      inference, and a tooltip naming it ("On Edge", "Perpendicular to Edge"). Used by every
      tool through the shared snapper. *Depends on:* 5 (and 1 makes it cleaner).
      *Accept:* draw a line perpendicular to an existing angled edge using the inference
      alone, with no typing.
- [x] **8. Draw on any face and plane.** Rectangle, Line and the curve tools work on the
      face under the pointer (tilted or vertical), with the plane locked once drawing starts;
      arrow keys pick a vertical or horizontal plane; rotated rectangles (three-click).
      *Depends on:* 4, 5. *Accept:* draw a window rectangle on a wall and push it in 100 mm.
- [x] **9. Circle and Polygon tools.** Center, radius, typed segment count (`24s`). Stored
      as true circles for display and inference (center snap), with segments in the mesh.
      *Depends on:* 4, 8. *Accept:* a 600 mm circle with 48 sides extrudes to a cylinder
      whose center snaps.
- [x] **10. Arc tools.** Two-point arc with bulge, three-point arc, pie, and tangent arcs
      when starting from an edge end (cyan tangent inference). *Depends on:* 9.
      *Accept:* a rounded rectangle drawn from lines and tangent arcs forms one face.
- [x] **11. Eraser (E).** Click or drag over edges to erase them (faces heal or vanish as in
      SketchUp); Shift hides, Ctrl softens. *Depends on:* 4, 5. *Accept:* erasing the edge
      between two coplanar faces leaves one face.
- [x] **12. Construction guides and points.** Tape Measure from an edge creates a parallel
      guide line, from a point creates a guide point; Ctrl toggles. Guides are snappable,
      can be hidden and deleted together (Edit > Delete Guides). Add the Protractor for
      angled guides. *Depends on:* 7. *Accept:* place a guide 450 mm in from a wall and snap
      a rectangle corner to it.
- [x] **13. Freehand tool** (smoothed polyline). *Depends on:* 6. Low priority.

Known limits from Phase 2, all waiting on item 14 unless noted: a rectangle drawn on a
wall pushes in as its own box instead of cutting a recess into the wall; erasing an edge
of a box turns it into free geometry that Push/Pull cannot use yet; and a single guide
cannot be selected or erased (only all at once with Edit > Delete Guides; this belongs
with selection, not item 14).

## Phase 3: Editing geometry

- [ ] **14. Push/pull any planar face.** Side faces, faces of any shape, negative distances
      that cut back, cutting all the way through to make an opening, Ctrl to create a new
      starting face instead of moving the existing one, double-click to repeat the last
      distance. Replaces today's cap-only push/pull. *Depends on:* 4, 5. *Accept:* push a
      window rectangle on a 200 mm wall in by 200 mm and get a hole; Ctrl-pull a cap twice
      to make a stepped block. *Started:* #18 added push/pull of a face drawn with the
      Line tool.
- [ ] **15. Move, Rotate and Scale on sub-elements.** Moving a vertex, edge or face stretches
      the connected geometry (with auto-fold when faces would go non-planar); Rotate a face
      about an edge; Scale with grip handles on the bounding box, mirrored with negative
      factors. *Depends on:* 5. *Accept:* move a box's top edge sideways to make a sloped
      roof; scale by `-1` mirrors.
- [ ] **16. Array copies.** After a Move copy, type `5x` (five copies) or `/5` (five spread
      between); the same after a Rotate copy for radial arrays. Editable until the next
      action. *Depends on:* 2. *Accept:* a fence of 10 posts from one post in two commands.
- [ ] **17. Offset tool (F).** Offset a face's boundary in or out by a typed distance,
      including concave shapes and faces with holes. *Depends on:* 4, 7. *Accept:* offset a
      wall's top face by 20 mm and push the ring up to make a coping.
- [ ] **18. Follow Me.** Sweep a profile face along a selected path of edges (open or
      closed), including around a face's perimeter. *Depends on:* 6, 10. *Accept:* a crown
      moulding profile swept around a rectangular room; a lathe-turned vase from a profile
      swept around a circle.
- [ ] **19. Intersect Faces.** With Model, With Selection, With Context: add edges where
      faces cross so they can be split and erased. *Depends on:* 4. *Accept:* intersecting
      two overlapping boxes adds the shared edges and erasing gives the union outline.
- [x] **20. Solid tools.** Union, Subtract, Intersect, Trim, Split and Outer Shell on
      closed solids (groups or components), probably through Manifold if item 3 picks it.
      *Depends on:* 3, 22. *Accept:* subtracting a cylinder from a box gives a closed solid
      with a round hole that passes `checkSolid`. *Done:* written on our own edge/face mesh
      (no kernel), working on today's closed objects, which groups and components can use
      once item 22 lands.
- [ ] **21. Edge and face cleanup.** Soften/smooth edges with an angle slider, Reverse
      Faces and Orient Faces, Hide/Unhide geometry, Flip Along an axis. *Depends on:* 5.
      *Accept:* a 48-sided cylinder can be shown smooth with no visible segment edges.

## Phase 4: Organizing a model

- [ ] **22. Groups.** Make Group (Ctrl+G), Explode, double-click to edit inside (rest of the
      model faded), Escape or click outside to leave, nested groups. Geometry inside a group
      does not stick to anything outside it. *Depends on:* 4, 5. *Accept:* group a box,
      draw a box touching it outside, and the two stay separate.
- [ ] **23. Components** (likely two PRs). Make Component with a name and an insertion
      axis, instances that share one definition (editing one edits all), Make Unique, glue
      to faces and "cut opening" for windows and doors, a Components panel listing
      definitions in the model. *Depends on:* 22. *Accept:* eight copies of a chair
      component update together when one is edited; a window component cuts its wall.
- [ ] **24. Outliner panel.** Tree of groups and components, rename, select from the tree,
      hide/lock toggles, search. *Depends on:* 22.
- [ ] **25. Tags (layers).** Assign tags to groups and components, toggle visibility, tag
      colors, tag folders. Untagged is the default. *Depends on:* 22. *Accept:* hide a
      "Furniture" tag and everything tagged with it disappears and cannot be picked.
- [ ] **26. Entity Info panel.** Replace the Properties panel with a context-sensitive
      panel: area of faces, length of edges, volume of solids, name, tag, material, locked,
      instance definition. *Depends on:* 5, 22.
- [ ] **27. Copy, cut and paste**, Paste in Place, between windows and between documents.
      *Depends on:* 2 (objects), 22 (geometry and groups).
- [ ] **28. Lock and hide** objects and groups; Unhide All. *Depends on:* 2.

## Phase 5: Appearance and views

- [ ] **29. Standard views and camera projection.** Top, Bottom, Front, Back, Left, Right
      and Iso views (toolbar and menu), Parallel Projection and Perspective, field of view,
      Zoom Window, Zoom Extents per selection, Previous view. *Depends on:* nothing.
      *Accept:* Top view in parallel projection shows a plan with the grid square on screen.
- [ ] **30. Walk, Look Around and Position Camera.** Eye-height walkthrough with
      collision off by default. *Depends on:* 29.
- [ ] **31. Materials and Paint Bucket (B).** Solid colors, opacity, a built-in starter
      library, sample (Alt), paint all matching (Shift), paint whole object (Ctrl). Saved
      in the project. *Depends on:* 5 for faces (object-level painting can come before).
      *Accept:* paint one face of a box red and the rest grey; save, reopen, unchanged.
- [ ] **32. Textures.** Image materials with real-world size, projected onto faces, and a
      texture position mode (move, rotate, scale the image on a face). Images are embedded
      or stored next to the project. *Depends on:* 31.
- [ ] **33. Styles.** Face styles (shaded, shaded with textures, monochrome, hidden line,
      wireframe, X-ray), edge settings (back edges dashed, profiles, extension, endpoints,
      color by axis), background and sky/ground. Saved per document. *Depends on:* 5.
- [ ] **34. Scenes.** Save camera, style and visible tags as a named scene, a scene tab bar,
      animated transitions between scenes. *Depends on:* 29, 33 (25 for tags).
- [ ] **35. Shadows.** Sun direction from date, time and the model's location, shadow
      maps for faces and ground, light/dark sliders. *Depends on:* 4. *Accept:* a box casts
      the expected shadow length at noon on the equinox for a given latitude.
- [ ] **36. Section planes.** Place a section plane on a face, cut the view, section fill,
      toggle visibility of planes and cuts, one active plane per context. *Depends on:* 22.
      *Accept:* a horizontal section 1.2 m up shows a building's floor plan.
- [ ] **37. Better annotations.** Text labels with leaders that read area, length or a
      name; radial and diameter dimensions on arcs and circles; angular dimensions; 3D Text
      that creates geometry. *Depends on:* 9 for radial, 4 for 3D Text.
- [ ] **38. Fog and edge-quality settings** (anti-aliasing level, line width scaling).
      Low priority.

## Phase 6: Getting models in and out

- [ ] **39. Export STL, OBJ and glTF/GLB.** Selection or whole model, unit choice, STL with
      a manifold warning per solid (`checkSolid` exists). Materials go to OBJ/MTL and glTF.
      *Depends on:* nothing for today's prisms; 31 for materials. *Accept:* an exported STL
      opens in a slicer at the right size.
- [ ] **40. 2D image export** (PNG, JPG) at a chosen resolution, with transparent
      background option. *Depends on:* nothing.
- [ ] **41. Import OBJ, STL and glTF** as a group or component, with a unit choice and
      merge of coplanar triangles into faces where possible. *Depends on:* 22.
- [ ] **42. Image import.** Place an image as a flat reference entity scaled by two points
      (for tracing a floor plan), or as a texture. *Depends on:* 32.
- [ ] **43. DXF import and export** (lines, arcs, polylines and 3D faces), for working with
      CAD drawings. *Depends on:* 6, 10. DWG through a converter is a separate question.
- [ ] **44. Vector export** of the current view (SVG and PDF) in parallel projection at a
      chosen scale, for printable plans. *Depends on:* 29, 33.
- [ ] **45. COLLADA (DAE) export**, the format many other tools still expect. Low priority.

## Phase 7: Everyday comfort and scale

- [ ] **46. Autosave and crash recovery.** Recovery file in the user's config directory
      every few minutes when there are unsaved changes; offer to restore on the next start.
      *Depends on:* nothing. *Accept:* kill the process after an edit; the next start
      offers the work back.
- [ ] **47. Preferences and templates.** Recent files, unit and precision settings per
      document (Model Info), default template (mm/inches, architectural vs woodworking),
      customizable keyboard shortcuts, mouse button options. *Depends on:* nothing.
- [ ] **48. Large-model performance.** BVH for picking and snapping, merged or instanced
      GPU buffers (instancing pays off once components exist), frustum culling. Extend the
      benchmark to 100,000 faces and record results. *Depends on:* 4; 23 for instancing.
- [ ] **49. Report generation.** A component and material list with counts, sizes and
      areas exported to CSV (a cut list for woodworkers). *Depends on:* 23, 26.
- [ ] **50. Scripting and extensions.** An embedded scripting API (Lua or Python) that can
      create geometry, add menu items and tools, with a documented, versioned API. This is
      how SketchUp gets most of its specialized features. *Depends on:* 1, 4, 22.
- [ ] **51. Terrain (sandbox) tools.** From Contours, From Scratch grid, Smoove, Stamp and
      Drape. *Depends on:* 4, 21. Low priority.
- [ ] **52. Automated viewport image tests.** Render known scenes offscreen (Mesa) and
      compare images in CI. *Depends on:* nothing; more valuable once 5 and 33 land.

## Phase 8: Distribution

- [ ] **53. Signed installers.** Code-signed Windows installer and a winget manifest; a
      signed, notarized macOS `.dmg`; a Linux AppImage or Flatpak. *Depends on:* signing
      certificates. *Accept:* no SmartScreen or Gatekeeper warnings.
- [ ] **54. Update check** (opt-in, off by default, matching the offline-first promise).
- [ ] **55. Optional Vulkan or Metal renderer** behind the existing `Renderer` interface.

## Not planned

- Reading or writing `.skp` files: proprietary, and a separate research task with no
  promise of support.
- A LayOut-style 2D document editor, an online model warehouse, cloud accounts and
  photorealistic rendering. Exports (items 39 to 44) and scripting (item 50) are the bridge
  to other tools for these.

## Start now

Phases 1 and 2 are done. **14** (push/pull any face) is the most pressing: the drawing
tools now make faces on walls and free geometry that only it can extrude or cut, and it
also unblocks the known limits above. Also open with no unfinished dependency: **15**
(sub-element transforms), **16** (array copies), **17** (Offset), **18** (Follow Me),
**19** (Intersect Faces), **21** (edge cleanup), **22** (groups), **28** (lock and hide),
**29** (views and projection), **31** (materials), **33** (styles), **35** (shadows),
**37** (better annotations, radial dimensions now that circles exist), **39**
(STL/OBJ/glTF export), **40** (image export), **43** (DXF), **46** (autosave), **47**
(preferences), **48** (large-model performance), **52** (image tests),
and the carried-over **0c** and **0d**.

Item 22 (groups) is still the critical path for phase 4.
