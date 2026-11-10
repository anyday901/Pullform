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

Viewport::Viewport(Renderer& renderer) : renderer_(renderer) {}

Viewport::~Viewport() {
    for (auto& [id, cached] : meshes_) renderer_.destroyMesh(cached.handle);
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
    sync(document);
    PointerInput in;
    in.screen = windowPos;
    in.ray = camera_.rayThrough(toLocal(windowPos), size_);
    for (const auto& [id, cached] : meshes_) {
        const auto hit = pickObject(in.ray, *cached.source, cached.mesh);
        if (hit && (!in.pick || hit->distance < in.pick->distance)) in.pick = hit;
    }
    return in;
}

void Viewport::sync(const Document& document) {
    // Drop meshes for objects that no longer exist.
    for (auto it = meshes_.begin(); it != meshes_.end();) {
        if (document.indexOf(it->first) < 0) {
            renderer_.destroyMesh(it->second.handle);
            it = meshes_.erase(it);
        } else {
            ++it;
        }
    }
    // Rebuild meshes whose document entry changed (pointer identity).
    for (const auto& obj : document.objects) {
        auto& cached = meshes_[obj->id];
        if (cached.source == obj) continue;
        if (cached.handle != kNoMesh) renderer_.destroyMesh(cached.handle);
        cached.source = obj;
        cached.mesh = buildMesh(*obj);
        cached.handle = renderer_.createMesh(cached.mesh);
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

void Viewport::drawFaceOverlay(const CachedMesh& cached, const FaceId& face, const glm::vec4& color) {
    for (const auto& range : cached.mesh.faces) {
        if (range.id == face) renderer_.drawMeshOverlay(cached.handle, range.firstIndex, range.indexCount, color);
    }
}

void Viewport::render(const Editor& editor, const ToolController& tools) {
    const Document& doc = editor.document();
    sync(doc);

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
    for (const auto& [id, cached] : meshes_) {
        if (pushPreview && pushPreview->objectId == id) continue; // replaced by the preview below
        renderer_.drawMesh(cached.handle, cached.mesh.closed ? kSolidFill : kSheetFill);
    }
    // Face highlights: one logical face, all of its triangles.
    for (const auto& [id, cached] : meshes_) {
        if (pushPreview && pushPreview->objectId == id) continue;
        if (hover && hover->objectId == id && hover->face && !(selection && *selection == *hover)) {
            drawFaceOverlay(cached, *hover->face, kHoverFace);
        }
        if (selection && selection->objectId == id && selection->face) drawFaceOverlay(cached, *selection->face, kSelectedFace);
    }
    // Real edges only (no triangulation diagonals); the selected object's edges in blue.
    lines.clear();
    std::vector<LineSegment> selectedLines;
    for (const auto& [id, cached] : meshes_) {
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
    renderer_.drawLines(lines, false);
    (void)tools;
    renderer_.endFrame();
}

} // namespace pf
