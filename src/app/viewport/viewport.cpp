#include "app/viewport/viewport.hpp"

#include "core/geometry/measure.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <initializer_list>

namespace pf {

namespace {

constexpr glm::vec4 kBackground{0.925f, 0.933f, 0.945f, 1.0f};
constexpr glm::vec4 kGridMinor{0.83f, 0.85f, 0.875f, 1.0f};
constexpr glm::vec4 kGridMajor{0.70f, 0.73f, 0.77f, 1.0f};
constexpr glm::vec4 kAxisX{0.86f, 0.20f, 0.18f, 1.0f};
constexpr glm::vec4 kAxisY{0.18f, 0.62f, 0.25f, 1.0f};
constexpr glm::vec4 kAxisZ{0.20f, 0.36f, 0.88f, 1.0f};
constexpr glm::vec4 kSolidFill{0.88f, 0.87f, 0.84f, 1.0f};
constexpr glm::vec4 kSheetFill{0.80f, 0.84f, 0.90f, 1.0f};
constexpr glm::vec4 kEdge{0.13f, 0.14f, 0.16f, 1.0f};
constexpr glm::vec4 kSelectedEdge{0.08f, 0.42f, 0.95f, 1.0f};
constexpr glm::vec4 kHoverFace{1.0f, 0.72f, 0.18f, 0.30f};
constexpr glm::vec4 kHoverEdge{1.0f, 0.62f, 0.10f, 1.0f};
constexpr glm::vec4 kSelectedFace{0.15f, 0.50f, 1.0f, 0.32f};
constexpr glm::vec4 kPreviewEdge{0.95f, 0.45f, 0.08f, 1.0f};
constexpr glm::vec4 kPreviewFill{0.95f, 0.55f, 0.15f, 0.35f};
constexpr glm::vec4 kErrorFill{0.90f, 0.15f, 0.12f, 0.40f};
constexpr glm::vec4 kErrorEdge{0.85f, 0.10f, 0.08f, 1.0f};
constexpr glm::vec4 kSnapEndpoint{0.10f, 0.65f, 0.20f, 1.0f};
constexpr glm::vec4 kSnapMidpoint{0.05f, 0.62f, 0.80f, 1.0f};
constexpr glm::vec4 kSnapOther{0.70f, 0.20f, 0.75f, 1.0f};
constexpr glm::vec4 kGuideFree{0.45f, 0.45f, 0.50f, 1.0f};
constexpr glm::vec4 kDimension{0.20f, 0.24f, 0.32f, 1.0f};
constexpr glm::vec4 kMeasure{0.95f, 0.45f, 0.08f, 1.0f};

glm::vec4 dim(glm::vec4 c, float a) { return {c.r, c.g, c.b, a}; }

glm::vec4 guideColor(int axis) {
    switch (axis) {
    case 0: return kAxisX;
    case 1: return kAxisY;
    case 2: return kAxisZ;
    default: return kGuideFree;
    }
}

glm::vec4 axisColor(MoveAxis axis) {
    switch (axis) {
    case MoveAxis::X: return kAxisX;
    case MoveAxis::Y: return kAxisY;
    case MoveAxis::Z: return kAxisZ;
    }
    return kAxisX;
}

/// Colour of a world axis direction: red X, green Y, blue Z.
glm::vec4 axisColor(const Vec3& axis) {
    if (axis.x != 0.0) return kAxisX;
    if (axis.y != 0.0) return kAxisY;
    return kAxisZ;
}

/// A unit vector perpendicular to `n`.
Vec3 perpendicular(const Vec3& n) {
    const Vec3 helper = std::abs(n.z) < 0.9 ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
    return glm::normalize(glm::cross(n, helper));
}

} // namespace

Viewport::Viewport(Renderer& renderer) : renderer_(renderer), meshes_(renderer) {}

Viewport::~Viewport() { releasePreviewMeshes(); }

void Viewport::releasePreviewMeshes() {
    for (const MeshHandle h : previewHandles_) renderer_.destroyMesh(h);
    previewHandles_.clear();
}

MeshHandle Viewport::previewMesh(const MeshData& mesh) {
    previewHandles_.push_back(renderer_.createMesh(mesh));
    return previewHandles_.back();
}

void Viewport::setRect(const Vec2& origin, const Vec2& size, const Vec2& framebufferScale, int framebufferHeight) {
    origin_ = origin;
    size_ = glm::max(size, Vec2{1.0, 1.0});
    framebufferScale_ = framebufferScale;
    framebufferHeight_ = framebufferHeight;
}

bool Viewport::contains(const Vec2& p) const {
    return p.x >= origin_.x && p.y >= origin_.y && p.x < origin_.x + size_.x && p.y < origin_.y + size_.y;
}

PointerInput Viewport::pointerAt(const Vec2& windowPos, const Document& document) {
    meshes_.sync(document);
    PointerInput in;
    in.screen = windowPos;
    in.ray = camera_.rayThrough(toLocal(windowPos), size_);
    in.pick = meshes_.pick(in.ray);
    in.pixelAngle = pixelAngle();
    // A copy of the camera, so the projector stays valid for the whole event.
    in.project = [camera = camera_, size = size_, origin = origin_](const Vec3& p) -> std::optional<Vec2> {
        const auto local = camera.project(p, size);
        if (!local) return std::nullopt;
        return *local + origin;
    };
    in.element = meshes_.pickElement(in.ray, ElementPickOptions{in.pixelAngle});
    return in;
}

double Viewport::pixelAngle() const { return 2.0 * std::tan(camera_.fovY() * 0.5) / std::max(size_.y, 1.0); }

void Viewport::drawSnap(std::vector<LineSegment>& lines, const SnapResult& snap) const {
    const Vec3 eye = camera_.position();
    const Vec3 f = camera_.forward();
    const Vec3 right = glm::normalize(glm::cross(f, Vec3{0.0, 0.0, 1.0}));
    const Vec3 up = glm::cross(right, f);
    const auto pixels = [&](const Vec3& at, double px) { return px * pixelAngle() * glm::length(at - eye); };

    // Inference guides: dashed lines in the color of their axis.
    for (const auto& g : snap.guides) {
        const double len = glm::length(g.to - g.from);
        if (len <= 0.0) continue;
        const double dash = std::max(pixels((g.from + g.to) * 0.5, 6.0), len / 400.0);
        const Vec3 dir = (g.to - g.from) / len;
        // An integer dash count (at most 200), not a floating-point loop counter.
        const auto count = static_cast<int>(std::ceil(len / (2.0 * dash)));
        for (int i = 0; i < count; ++i) {
            const double s = 2.0 * dash * i;
            lines.emplace_back(g.from + dir * s, g.from + dir * std::min(s + dash, len), guideColor(g.axis));
        }
    }

    // Marker at the snapped point, facing the camera: a square on an endpoint,
    // a triangle on a midpoint, a diamond for an inference or face point.
    const Vec3 p = snap.point;
    const double r = pixels(p, 6.0);
    const auto poly = [&](std::initializer_list<Vec2> corners, const glm::vec4& color) {
        std::vector<Vec3> pts;
        for (const auto& c : corners) pts.push_back(p + right * (c.x * r) + up * (c.y * r));
        for (std::size_t i = 0; i < pts.size(); ++i) lines.emplace_back(pts[i], pts[(i + 1) % pts.size()], color);
    };
    switch (snap.kind) {
    case SnapKind::Endpoint: poly({{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}, kSnapEndpoint); break;
    case SnapKind::Midpoint: poly({{-1.1, -0.8}, {1.1, -0.8}, {0, 1.1}}, kSnapMidpoint); break;
    case SnapKind::Inference:
    case SnapKind::OnFace: poly({{0, -1.2}, {1.2, 0}, {0, 1.2}, {-1.2, 0}}, kSnapOther); break;
    case SnapKind::Grid:
    case SnapKind::None: break;
    }
}

void Viewport::drawGrid(std::vector<LineSegment>& lines) {
    // Grid spacing adapts to the zoom level in powers of ten (100 mm at the
    // default view). Lines fade out toward the edge to avoid distant aliasing.
    const double raw = camera_.distance() * 0.03;
    gridStep_ = std::clamp(std::pow(10.0, std::floor(std::log10(raw))), 0.001, 100.0);
    const double major = gridStep_ * 10.0;
    constexpr int half = 150; // minor lines on each side of the center
    const Vec3 t = camera_.target();
    const double cx = std::round(t.x / major) * major;
    const double cy = std::round(t.y / major) * major;
    const double ext = half * gridStep_;
    const auto faded = [](glm::vec4 c, double f) { return glm::vec4(c.r, c.g, c.b, c.a * static_cast<float>(f)); };
    const auto fade = [&](int i) { const double u = std::abs(i) / double(half); return std::max(0.0, 1.0 - u * u); };
    const auto addLine = [&](Vec3 center, Vec3 dir, glm::vec4 c, double f) {
        // Two halves from the center outward: opaque in the middle, transparent at the ends.
        lines.push_back({center, center + dir * ext, faded(c, f), faded(c, 0.0)});
        lines.push_back({center, center - dir * ext, faded(c, f), faded(c, 0.0)});
    };
    for (int i = -half; i <= half; ++i) {
        const double x = cx + i * gridStep_;
        const double y = cy + i * gridStep_;
        const bool majorX = std::abs(std::remainder(x, major)) < gridStep_ * 0.01;
        const bool majorY = std::abs(std::remainder(y, major)) < gridStep_ * 0.01;
        if (std::abs(x) > gridStep_ * 0.01) addLine({x, cy, 0}, {0, 1, 0}, majorX ? kGridMajor : kGridMinor, fade(i));
        if (std::abs(y) > gridStep_ * 0.01) addLine({cx, y, 0}, {1, 0, 0}, majorY ? kGridMajor : kGridMinor, fade(i));
    }
}

void Viewport::appendEdges(std::vector<LineSegment>& lines, const MeshData& mesh, const glm::vec4& color) {
    for (std::size_t i = 0; i + 1 < mesh.edges.size(); i += 2) lines.push_back({mesh.edges[i], mesh.edges[i + 1], color});
}

std::optional<Vec2> Viewport::toWindow(const Vec3& world) const {
    const auto p = camera_.project(world, size_);
    if (!p) return std::nullopt;
    return origin_ + *p;
}

void Viewport::appendDimension(std::vector<LineSegment>& lines, const Vec3& a, const Vec3& b, const Vec3& offset,
                               const glm::vec4& color) const {
    const DimensionLayout l = layoutDimension(a, b, offset);
    const double px = camera_.worldPerPixel(l.label, size_);
    const double offLen = glm::length(offset);
    const Vec3 offDir = offLen > 1e-12 ? offset / offLen : Vec3{0.0};
    // Extension lines from the measured points, running a few pixels past the
    // dimension line; a small gap keeps them off the model's own edges.
    if (offLen > 1e-12) {
        const double gap = std::min(4.0 * px, offLen * 0.5);
        lines.push_back({l.a + offDir * gap, l.lineA + offDir * (6.0 * px), color});
        lines.push_back({l.b + offDir * gap, l.lineB + offDir * (6.0 * px), color});
    }
    lines.push_back({l.lineA, l.lineB, color});
    // 45-degree ticks at both ends of the dimension line.
    if (l.length > 1e-12) {
        const Vec3 u = (l.b - l.a) / l.length;
        Vec3 side = offLen > 1e-12 ? offDir : glm::cross(u, camera_.forward());
        if (glm::length(side) < 1e-9) side = Vec3{0.0, 0.0, 1.0};
        const Vec3 tick = glm::normalize(u + glm::normalize(side)) * (5.0 * px);
        lines.push_back({l.lineA - tick, l.lineA + tick, color});
        lines.push_back({l.lineB - tick, l.lineB + tick, color});
    }
}

std::vector<ViewportLabel> Viewport::labels(const Editor& editor) const {
    std::vector<ViewportLabel> out;
    const Document& doc = editor.document();
    const LengthUnit u = doc.displayUnit;
    for (const auto& d : doc.dimensions) {
        const auto l = layoutDimension(doc, d);
        if (!l) continue;
        if (const auto p = toWindow(l->label)) out.push_back({*p, formatLength(l->length, u), ViewportLabel::Style::Dimension});
    }
    if (const auto* m = std::get_if<MeasurePreview>(&editor.preview()); m && m->start && m->end) {
        const Vec3 offset = m->offset.value_or(Vec3{0.0});
        const DimensionLayout l = layoutDimension(*m->start, *m->end, m->mode == MeasurePreview::Mode::Dimension ? offset : Vec3{0.0});
        if (l.length > 0.0) {
            if (const auto p = toWindow(l.label)) out.push_back({*p, formatLength(l.length, u), ViewportLabel::Style::Live});
        }
    }
    return out;
}

void Viewport::drawFaceOverlay(const CachedMesh& cached, const FaceId& face, const glm::vec4& color) {
    for (const auto& range : cached.mesh.faces) {
        if (range.id == face) renderer_.drawMeshOverlay(cached.handle, range.firstIndex, range.indexCount, color);
    }
}

void Viewport::appendVertexMarker(std::vector<LineSegment>& lines, const Vec3& p, double pixels, const glm::vec4& color) const {
    const Vec3 f = camera_.forward();
    Vec3 right = glm::cross(f, Vec3{0.0, 0.0, 1.0});
    right = glm::length(right) > 1e-9 ? glm::normalize(right) : Vec3{1.0, 0.0, 0.0};
    const Vec3 up = glm::cross(right, f);
    const double r = pixels * camera_.worldPerPixel(p, size_);
    const std::array<Vec3, 4> c = {p + (-right - up) * r, p + (right - up) * r, p + (right + up) * r, p + (-right + up) * r};
    for (std::size_t i = 0; i < c.size(); ++i) lines.emplace_back(c[i], c[(i + 1) % c.size()], color);
}

namespace {

/// Appends one face's triangles (both sides for a two-sided face) to an overlay mesh.
void appendFaceTriangles(MeshData& out, const ElementTopology& topo, std::uint32_t face) {
    const auto& f = topo.faces[face];
    for (const auto& tri : topo.triangles) {
        if (tri.face != face) continue;
        const auto emit = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c, const Vec3& n) {
            const auto base = static_cast<std::uint32_t>(out.positions.size());
            for (const auto v : {a, b, c}) {
                out.positions.push_back(topo.vertices[v].position);
                out.normals.push_back(n);
            }
            out.indices.insert(out.indices.end(), {base, base + 1, base + 2});
        };
        emit(tri.v[0], tri.v[1], tri.v[2], f.normal);
        if (f.twoSided) emit(tri.v[0], tri.v[2], tri.v[1], -f.normal);
    }
}

} // namespace

void Viewport::drawElements(const Editor& editor, const std::vector<ObjectId>& previewed, std::vector<LineSegment>& onTop) {
    const auto isPreviewed = [&previewed](ObjectId id) { return std::ranges::find(previewed, id) != previewed.end(); };
    const auto& set = editor.selectionSet();
    MeshData selectedFaces;
    MeshData hoverFaces;
    std::vector<LineSegment> selectedEdges;
    std::vector<LineSegment> hoverEdges;
    // Adds one element's highlight: face triangles, an edge line or a vertex marker.
    const auto append = [this, &isPreviewed, &onTop](const ElementRef& ref, MeshData& faces, std::vector<LineSegment>& edges,
                                                     double markerPixels, const glm::vec4& color) {
        if (isPreviewed(ref.object)) return;
        const auto* cached = meshes_.find(ref.object);
        if (!cached) return;
        const auto& topo = cached->topology;
        const auto index = topo.indexOf(ref.kind, ref.id);
        if (!index.has_value()) return;
        using enum ElementKind;
        switch (ref.kind) {
        case Face: appendFaceTriangles(faces, topo, *index); break;
        case Edge: {
            const auto& e = topo.edges[*index];
            edges.emplace_back(topo.vertices[e.a].position, topo.vertices[e.b].position, color);
            break;
        }
        case Vertex: appendVertexMarker(onTop, topo.vertices[*index].position, markerPixels, color); break;
        }
    };
    for (const auto& ref : set.elements()) append(ref, selectedFaces, selectedEdges, 4.0, kSelectedEdge);
    if (const auto& hover = editor.hoverElement(); hover.has_value() && !set.containsElement(*hover)) {
        append(*hover, hoverFaces, hoverEdges, 5.0, kHoverEdge);
    }
    // Face overlays from the topology's own triangles, uploaded for this frame
    // only (a handful of faces), so they work for any mesh.
    for (const auto& [faces, color] : {std::pair{&selectedFaces, kSelectedFace}, std::pair{&hoverFaces, kHoverFace}}) {
        if (faces->indices.empty()) continue;
        const MeshHandle handle = renderer_.createMesh(*faces);
        renderer_.drawMeshOverlay(handle, 0, static_cast<std::uint32_t>(faces->indices.size()), color);
        renderer_.destroyMesh(handle);
    }
    renderer_.drawWideLines(selectedEdges, kHighlightWidthPx, true);
    renderer_.drawWideLines(hoverEdges, kHighlightWidthPx, true);
}

void Viewport::drawTransformResult(const std::optional<ProfileObject>& result, bool error, const ProfileObject* original,
                                   std::vector<LineSegment>& lines) {
    if (result) {
        const MeshData mesh = buildMesh(*result);
        renderer_.drawMesh(previewMesh(mesh), glm::vec4(0.70f, 0.82f, 0.97f, 1.0f));
        appendEdges(lines, mesh, kSelectedEdge);
        if (original) appendEdges(lines, buildMesh(*original), dim(kEdge, 0.35f)); // where it was
    } else if (error && original) {
        appendEdges(lines, buildMesh(*original), kErrorEdge);
    }
}

void Viewport::drawTransformGroup(const std::vector<ProfileObject>& group, const std::vector<ObjectId>& ids,
                                  const std::optional<ProfileObject>& result, bool error, ObjectId primary,
                                  const Document& doc, std::vector<LineSegment>& lines) {
    if (ids.empty()) {
        drawTransformResult(result, error, doc.find(primary), lines);
        return;
    }
    if (group.empty()) { // invalid value: every original in red
        for (const ObjectId id : ids) drawTransformResult(std::nullopt, error, doc.find(id), lines);
        return;
    }
    for (const auto& o : group) drawTransformResult(o, false, doc.find(o.id), lines);
}

void Viewport::render(const Editor& editor, const ToolController& tools) {
    const Document& doc = editor.document();
    meshes_.sync(doc);

    FrameSetup frame;
    frame.view = camera_.view();
    frame.projection = camera_.projection(aspect());
    frame.eye = camera_.position();
    frame.x = static_cast<int>(std::lround(origin_.x * framebufferScale_.x));
    frame.width = std::max(1, static_cast<int>(std::lround(size_.x * framebufferScale_.x)));
    frame.height = std::max(1, static_cast<int>(std::lround(size_.y * framebufferScale_.y)));
    frame.y = framebufferHeight_ - static_cast<int>(std::lround(origin_.y * framebufferScale_.y)) - frame.height;
    frame.clearColor = kBackground;
    renderer_.beginFrame(frame);

    // Grid and axes first (depth-tested so solids hide them).
    std::vector<LineSegment> lines;
    drawGrid(lines);
    const double axisLen = std::max(camera_.distance() * 3.0, 10.0);
    lines.push_back({{0, 0, 0}, {axisLen, 0, 0}, kAxisX});
    lines.push_back({{0, 0, 0}, {0, axisLen, 0}, kAxisY});
    lines.push_back({{0, 0, 0}, {0, 0, axisLen}, kAxisZ});
    lines.push_back({{0, 0, 0}, {-axisLen, 0, 0}, dim(kAxisX, 0.25f)});
    lines.push_back({{0, 0, 0}, {0, -axisLen, 0}, dim(kAxisY, 0.25f)});
    lines.push_back({{0, 0, 0}, {0, 0, -axisLen}, dim(kAxisZ, 0.25f)});
    renderer_.drawLines(lines, true);

    const auto* pushPreview = std::get_if<PushPullPreview>(&editor.preview());
    const auto* movePreview = std::get_if<MovePreview>(&editor.preview());
    // The objects a preview stands in for are drawn by the preview below
    // (a move copy leaves the originals drawn).
    const std::vector<ObjectId> replaced = replacedObjects(editor.preview());
    const auto isPreviewed = [&replaced](ObjectId id) { return std::ranges::find(replaced, id) != replaced.end(); };
    const auto& selection = editor.selection();
    const auto* box = std::get_if<SelectBoxPreview>(&editor.preview());
    const auto& hover = editor.hover();

    // Solid fills.
    for (const auto& [id, cached] : meshes_.entries()) {
        if (isPreviewed(id)) continue; // replaced by the preview below
        renderer_.drawMesh(cached.handle, cached.mesh.closed ? kSolidFill : kSheetFill);
    }
    // Face highlights: one logical face, all of its triangles. An object
    // selected through its elements shows those instead (drawElements).
    const auto& set = editor.selectionSet();
    for (const auto& [id, cached] : meshes_.entries()) {
        if (isPreviewed(id)) continue;
        if (hover && hover->objectId == id && hover->face && !(selection && *selection == *hover)) {
            drawFaceOverlay(cached, *hover->face, kHoverFace);
        }
        if (selection && selection->objectId == id && selection->face && !set.hasElements(id)) {
            drawFaceOverlay(cached, *selection->face, kSelectedFace);
        }
    }
    // Real edges only (no triangulation diagonals); wholly selected objects' edges
    // in blue. With the profiles style, outline edges are drawn wide on top.
    lines.clear();
    std::vector<LineSegment> selectedLines;
    std::vector<LineSegment> profileLines;
    std::vector<LineSegment> selectedProfileLines;
    const Vec3 eye = camera_.position();
    for (const auto& [id, cached] : meshes_.entries()) {
        if (isPreviewed(id)) {
            // A moving object leaves a faint outline where it started.
            if (movePreview) appendEdges(lines, cached.mesh, dim(kEdge, 0.25f));
            continue;
        }
        // While dragging a selection box, the objects it would select show as selected.
        const bool selected = box ? std::ranges::find(box->found, id) != box->found.end()
                                  : set.contains(id) && !set.hasElements(id);
        // From the topology, so loose edges (no face) of a mesh group draw too.
        for (const auto& e : cached.topology.edges) {
            if (e.soft) continue;
            (selected ? selectedLines : lines)
                .emplace_back(cached.topology.vertices[e.a].position, cached.topology.vertices[e.b].position,
                              selected ? kSelectedEdge : kEdge);
        }
        if (profileEdges_) {
            const auto& topo = cached.topology;
            for (const auto e : pf::profileEdges(topo, eye)) {
                (selected ? selectedProfileLines : profileLines)
                    .emplace_back(topo.vertices[topo.edges[e].a].position, topo.vertices[topo.edges[e].b].position,
                                  selected ? kSelectedEdge : kEdge);
            }
        }
    }
    renderer_.drawLines(lines, true);
    renderer_.drawLines(selectedLines, true);
    renderer_.drawWideLines(profileLines, kProfileWidthPx, true);
    renderer_.drawWideLines(selectedProfileLines, kProfileWidthPx, true);
    std::vector<LineSegment> elementMarks;
    drawElements(editor, replaced, elementMarks);

    // Previews: transient, never part of the document.
    releasePreviewMeshes();
    lines.clear();
    if (const auto* rect = std::get_if<RectanglePreview>(&editor.preview())) {
        const Vec3 a = rect->start;
        const Vec3 b = rect->end;
        const glm::vec4 color = rect->placement ? kPreviewEdge : kErrorEdge;
        lines.push_back({{a.x, a.y, 0}, {b.x, a.y, 0}, color});
        lines.push_back({{b.x, a.y, 0}, {b.x, b.y, 0}, color});
        lines.push_back({{b.x, b.y, 0}, {a.x, b.y, 0}, color});
        lines.push_back({{a.x, b.y, 0}, {a.x, a.y, 0}, color});
        if (rect->placement) {
            ProfileObject temp;
            temp.profile.boundary = rectangleBoundary(rect->placement->width, rect->placement->depth);
            temp.transform.translation = rect->placement->corner;
            renderer_.drawMesh(previewMesh(buildMesh(temp)), kPreviewFill);
        }
    } else if (pushPreview) {
        if (const auto* obj = doc.find(pushPreview->objectId)) {
            const bool valid = !pushPreview->error.has_value();
            const double shown = valid ? pushPreview->height : std::max(pushPreview->height, 1e-4);
            if (shown > 0.0) {
                const MeshData mesh = buildMesh(*obj, shown);
                renderer_.drawMesh(previewMesh(mesh), valid ? glm::vec4(0.70f, 0.82f, 0.97f, 1.0f) : kErrorFill);
                appendEdges(lines, mesh, valid ? kSelectedEdge : kErrorEdge);
            } else {
                appendEdges(lines, buildMesh(*obj), kErrorEdge);
            }
        }
    } else if (movePreview) {
        const bool valid = !movePreview->error.has_value();
        const std::vector<ObjectId> moving =
            movePreview->objectIds.empty() ? std::vector<ObjectId>{movePreview->objectId} : movePreview->objectIds;
        for (const ObjectId id : moving) {
            const auto* obj = doc.find(id);
            if (!obj) continue;
            ProfileObject moved = *obj;
            moved.transform.translation += movePreview->offset;
            const MeshData mesh = buildMesh(moved);
            glm::vec4 fill = mesh.closed ? kSolidFill : kSheetFill;
            if (!valid) fill = kErrorFill;
            renderer_.drawMesh(previewMesh(mesh), fill);
            appendEdges(lines, mesh, valid ? kSelectedEdge : kErrorEdge);
        }
        if (doc.find(movePreview->objectId)) {
            // Guide from the picked point to where it is now, in the locked axis colour.
            glm::vec4 guide = kPreviewEdge;
            if (movePreview->axis) {
                guide = axisColor(*movePreview->axis);
                // Extend the locked axis through the start point so the constraint is visible.
                const Vec3 dir = axisVector(*movePreview->axis);
                const double reach = std::max(camera_.distance(), 1.0);
                lines.push_back({movePreview->from - dir * reach, movePreview->from + dir * reach, dim(guide, 0.35f)});
            }
            if (!valid) guide = kErrorEdge;
            lines.push_back({movePreview->from, movePreview->from + movePreview->offset, guide});
        }
    }
    if (const auto* rot = std::get_if<RotatePreview>(&editor.preview())) {
        drawTransformGroup(rot->group, rot->objectIds, rot->result, rot->error.has_value(), rot->objectId, doc, lines);
        const glm::vec4 color = axisColor(rot->axis);
        // Protractor: a circle in the rotation plane, the reference and current spokes.
        const auto spoke = rot->reference ? rot->reference : rot->current;
        double radius = spoke ? glm::length(*spoke) : 0.0;
        if (radius < 1e-9) radius = camera_.distance() * 0.05;
        const Vec3 n = rot->axis;
        const Vec3 u = spoke && glm::length(*spoke) > 1e-9 ? glm::normalize(*spoke) : perpendicular(n);
        const Vec3 v = glm::cross(n, u);
        constexpr int kSegments = 72;
        for (int i = 0; i < kSegments; ++i) {
            const double a0 = 2.0 * std::numbers::pi * i / kSegments;
            const double a1 = 2.0 * std::numbers::pi * (i + 1) / kSegments;
            lines.push_back({rot->pivot + (u * std::cos(a0) + v * std::sin(a0)) * radius,
                             rot->pivot + (u * std::cos(a1) + v * std::sin(a1)) * radius, color});
        }
        lines.push_back({rot->pivot - n * radius * 0.25, rot->pivot + n * radius * 0.25, color});
        if (rot->reference) lines.push_back({rot->pivot, rot->pivot + *rot->reference, kPreviewEdge});
        if (rot->current) lines.push_back({rot->pivot, rot->pivot + *rot->current, rot->error ? kErrorEdge : kPreviewEdge});
    } else if (const auto* sc = std::get_if<ScalePreview>(&editor.preview())) {
        drawTransformGroup(sc->group, sc->objectIds, sc->result, sc->error.has_value(), sc->objectId, doc, lines);
        if (sc->start) lines.push_back({sc->pivot, *sc->start, dim(kPreviewEdge, 0.5f)});
        if (sc->current) lines.push_back({sc->pivot, *sc->current, sc->error ? kErrorEdge : kPreviewEdge});
    }
    // Dimensions and measurements draw on top so they stay readable.
    for (const auto& d : doc.dimensions) {
        const auto a = resolveAnchor(doc, d.a);
        const auto b = resolveAnchor(doc, d.b);
        if (a && b) appendDimension(lines, *a, *b, d.offset, kDimension);
    }
    if (const auto* m = std::get_if<MeasurePreview>(&editor.preview())) {
        if (m->start && m->end) {
            if (m->mode == MeasurePreview::Mode::Dimension && m->fixed) {
                appendDimension(lines, *m->start, *m->end, m->offset.value_or(Vec3{0.0}), kMeasure);
            } else {
                lines.push_back({*m->start, *m->end, kMeasure});
            }
        }
    }
    if (const auto& snap = tools.snapIndicator()) drawSnap(lines, *snap);
    renderer_.drawLines(lines, false);
    renderer_.drawWideLines(elementMarks, 2.0f, false);
    renderer_.endFrame();
}

} // namespace pf
