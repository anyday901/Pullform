#include "app/viewport/viewport.hpp"

#include "core/geometry/measure.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
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

} // namespace

Viewport::Viewport(Renderer& renderer) : renderer_(renderer), meshes_(renderer) {}

Viewport::~Viewport() {
    if (previewHandle_ != kNoMesh) renderer_.destroyMesh(previewHandle_);
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
        const glm::vec4 color = g.axis == 0 ? kAxisX : g.axis == 1 ? kAxisY : g.axis == 2 ? kAxisZ : kGuideFree;
        const double len = glm::length(g.to - g.from);
        if (len <= 0.0) continue;
        const double dash = std::max(pixels((g.from + g.to) * 0.5, 6.0), len / 400.0);
        const Vec3 dir = (g.to - g.from) / len;
        for (double s = 0.0; s < len; s += 2.0 * dash) lines.push_back({g.from + dir * s, g.from + dir * std::min(s + dash, len), color});
    }

    // Marker at the snapped point, facing the camera: a square on an endpoint,
    // a triangle on a midpoint, a diamond for an inference or face point.
    const Vec3 p = snap.point;
    const double r = pixels(p, 6.0);
    const auto poly = [&](std::initializer_list<Vec2> corners, const glm::vec4& color) {
        std::vector<Vec3> pts;
        for (const auto& c : corners) pts.push_back(p + right * (c.x * r) + up * (c.y * r));
        for (std::size_t i = 0; i < pts.size(); ++i) lines.push_back({pts[i], pts[(i + 1) % pts.size()], color});
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
    const auto& selection = editor.selection();
    const auto& hover = editor.hover();

    // Solid fills.
    for (const auto& [id, cached] : meshes_.entries()) {
        if (pushPreview && pushPreview->objectId == id) continue; // replaced by the preview below
        renderer_.drawMesh(cached.handle, cached.mesh.closed ? kSolidFill : kSheetFill);
    }
    // Face highlights: one logical face, all of its triangles.
    for (const auto& [id, cached] : meshes_.entries()) {
        if (pushPreview && pushPreview->objectId == id) continue;
        if (hover && hover->objectId == id && hover->face && !(selection && *selection == *hover)) {
            drawFaceOverlay(cached, *hover->face, kHoverFace);
        }
        if (selection && selection->objectId == id && selection->face) drawFaceOverlay(cached, *selection->face, kSelectedFace);
    }
    // Real edges only (no triangulation diagonals); the selected object's edges in blue.
    lines.clear();
    std::vector<LineSegment> selectedLines;
    for (const auto& [id, cached] : meshes_.entries()) {
        if (pushPreview && pushPreview->objectId == id) continue;
        const bool selected = selection && selection->objectId == id;
        appendEdges(selected ? selectedLines : lines, cached.mesh, selected ? kSelectedEdge : kEdge);
    }
    renderer_.drawLines(lines, true);
    renderer_.drawLines(selectedLines, true);

    // Previews: transient, never part of the document.
    if (previewHandle_ != kNoMesh) {
        renderer_.destroyMesh(previewHandle_);
        previewHandle_ = kNoMesh;
    }
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
            previewHandle_ = renderer_.createMesh(buildMesh(temp));
            renderer_.drawMesh(previewHandle_, kPreviewFill);
        }
    } else if (pushPreview) {
        if (const auto* obj = doc.find(pushPreview->objectId)) {
            const bool valid = !pushPreview->error.has_value();
            const double shown = valid ? pushPreview->height : std::max(pushPreview->height, 1e-4);
            if (shown > 0.0) {
                const MeshData mesh = buildMesh(*obj, shown);
                previewHandle_ = renderer_.createMesh(mesh);
                renderer_.drawMesh(previewHandle_, valid ? glm::vec4(0.70f, 0.82f, 0.97f, 1.0f) : kErrorFill);
                appendEdges(lines, mesh, valid ? kSelectedEdge : kErrorEdge);
            } else {
                appendEdges(lines, buildMesh(*obj), kErrorEdge);
            }
        }
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
    renderer_.endFrame();
}

} // namespace pf
