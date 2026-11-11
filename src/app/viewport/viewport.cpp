#include "app/viewport/viewport.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

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
    return in;
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

void Viewport::drawFaceOverlay(const CachedMesh& cached, const FaceId& face, const glm::vec4& color) {
    for (const auto& range : cached.mesh.faces) {
        if (range.id == face) renderer_.drawMeshOverlay(cached.handle, range.firstIndex, range.indexCount, color);
    }
}

void Viewport::drawTransformResult(const std::optional<ProfileObject>& result, bool error, const ProfileObject* original,
                                   std::vector<LineSegment>& lines) {
    if (result) {
        const MeshData mesh = buildMesh(*result);
        previewHandle_ = renderer_.createMesh(mesh);
        renderer_.drawMesh(previewHandle_, glm::vec4(0.70f, 0.82f, 0.97f, 1.0f));
        appendEdges(lines, mesh, kSelectedEdge);
        if (original) appendEdges(lines, buildMesh(*original), dim(kEdge, 0.35f)); // where it was
    } else if (error && original) {
        appendEdges(lines, buildMesh(*original), kErrorEdge);
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
    const std::optional<ObjectId> hidden = replacedObject(editor.preview()); // drawn by the preview below
    const auto& selection = editor.selection();
    const auto& hover = editor.hover();

    // Solid fills.
    for (const auto& [id, cached] : meshes_.entries()) {
        if (hidden == id) continue;
        renderer_.drawMesh(cached.handle, cached.mesh.closed ? kSolidFill : kSheetFill);
    }
    // Face highlights: one logical face, all of its triangles.
    for (const auto& [id, cached] : meshes_.entries()) {
        if (hidden == id) continue;
        if (hover && hover->objectId == id && hover->face && !(selection && *selection == *hover)) {
            drawFaceOverlay(cached, *hover->face, kHoverFace);
        }
        if (selection && selection->objectId == id && selection->face) drawFaceOverlay(cached, *selection->face, kSelectedFace);
    }
    // Real edges only (no triangulation diagonals); the selected object's edges in blue.
    lines.clear();
    std::vector<LineSegment> selectedLines;
    for (const auto& [id, cached] : meshes_.entries()) {
        if (hidden == id) continue;
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
    if (const auto* rot = std::get_if<RotatePreview>(&editor.preview())) {
        drawTransformResult(rot->result, rot->error.has_value(), doc.find(rot->objectId), lines);
        const glm::vec4 axisColor = rot->axis.x != 0.0 ? kAxisX : rot->axis.y != 0.0 ? kAxisY : kAxisZ;
        // Protractor: a circle in the rotation plane, the reference and current spokes.
        const auto spoke = rot->reference ? rot->reference : rot->current;
        double radius = spoke ? glm::length(*spoke) : 0.0;
        if (radius < 1e-9) radius = camera_.distance() * 0.05;
        const Vec3 n = rot->axis;
        const Vec3 u = spoke && glm::length(*spoke) > 1e-9 ? glm::normalize(*spoke)
                                                            : glm::normalize(glm::cross(n, std::abs(n.z) < 0.9 ? Vec3{0, 0, 1} : Vec3{1, 0, 0}));
        const Vec3 v = glm::cross(n, u);
        constexpr int kSegments = 72;
        for (int i = 0; i < kSegments; ++i) {
            const double a0 = 2.0 * 3.14159265358979323846 * i / kSegments;
            const double a1 = 2.0 * 3.14159265358979323846 * (i + 1) / kSegments;
            lines.push_back({rot->pivot + (u * std::cos(a0) + v * std::sin(a0)) * radius,
                             rot->pivot + (u * std::cos(a1) + v * std::sin(a1)) * radius, axisColor});
        }
        lines.push_back({rot->pivot - n * radius * 0.25, rot->pivot + n * radius * 0.25, axisColor});
        if (rot->reference) lines.push_back({rot->pivot, rot->pivot + *rot->reference, kPreviewEdge});
        if (rot->current) lines.push_back({rot->pivot, rot->pivot + *rot->current, rot->error ? kErrorEdge : kPreviewEdge});
    } else if (const auto* sc = std::get_if<ScalePreview>(&editor.preview())) {
        drawTransformResult(sc->result, sc->error.has_value(), doc.find(sc->objectId), lines);
        if (sc->start) lines.push_back({sc->pivot, *sc->start, dim(kPreviewEdge, 0.5f)});
        if (sc->current) lines.push_back({sc->pivot, *sc->current, sc->error ? kErrorEdge : kPreviewEdge});
    }
    renderer_.drawLines(lines, false);
    (void)tools;
    renderer_.endFrame();
}

} // namespace pf
