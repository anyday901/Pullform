#include "core/commands/cleanup.hpp"

#include "core/commands/object_edit.hpp"
#include "core/geometry/mesh.hpp"
#include "core/geometry/solid.hpp"

#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>

#include <algorithm>
#include <cmath>
#include <deque>
#include <iterator>
#include <set>
#include <string>

namespace pf {

namespace {

/// The edges and faces of one object that a command acts on.
struct Targets {
    ObjectId object;
    std::vector<mesh::EdgeId> edges;
    std::vector<mesh::FaceId> faces;
};

template <typename T>
void addUnique(std::vector<T>& list, const T& value) {
    if (std::ranges::find(list, value) == list.end()) list.push_back(value);
}

Targets& targetsFor(std::vector<Targets>& all, ObjectId id) {
    if (auto it = std::ranges::find_if(all, [id](const Targets& t) { return t.object == id; }); it != all.end()) return *it;
    all.push_back(Targets{id, {}, {}});
    return all.back();
}

/// Every edge and face of an object selected whole.
void addWholeObject(std::vector<Targets>& all, const ProfileObject& o) {
    const auto m = mesh::objectMesh(o);
    Targets& t = targetsFor(all, o.id);
    for (const auto& [eid, e] : m->edges()) addUnique(t.edges, mesh::EdgeId{eid});
    for (const auto& [fid, f] : m->faces()) addUnique(t.faces, mesh::FaceId{fid});
}

/// One selected edge or face (with `faceEdges`, a face brings its bounding edges).
void addElement(std::vector<Targets>& all, const ProfileObject& o, const ElementRef& ref, bool faceEdges) {
    const auto m = mesh::objectMesh(o);
    if (ref.kind == ElementKind::Edge && m->edge(mesh::EdgeId{ref.id})) {
        addUnique(targetsFor(all, ref.object).edges, mesh::EdgeId{ref.id});
        return;
    }
    if (ref.kind != ElementKind::Face || !m->face(mesh::FaceId{ref.id})) return;
    Targets& t = targetsFor(all, ref.object);
    addUnique(t.faces, mesh::FaceId{ref.id});
    if (!faceEdges) return;
    for (const auto e : m->edgesOfFace(mesh::FaceId{ref.id})) addUnique(t.edges, e);
}

/// Resolves a scope against the document, objects in first-seen order.
std::vector<Targets> resolve(const Document& doc, const CleanupScope& scope, bool faceEdges) {
    std::vector<Targets> all;
    for (const ObjectId id : scope.objects) {
        if (const auto* o = doc.find(id)) addWholeObject(all, *o);
    }
    for (const auto& ref : scope.elements) {
        if (const auto* o = doc.find(ref.object)) addElement(all, *o, ref, faceEdges);
    }
    return all;
}

/// Runs `edit(mesh, targets) -> bool` on the mesh of every target object;
/// objects it reports as changed are replaced (validated), the rest are left
/// as they are. Fails with `unchanged` when nothing changed.
template <typename Edit>
Expected<OpResult> editMeshes(const Document& doc, const std::vector<Targets>& targets, Edit&& edit, const std::string& unchanged) {
    OpResult result{doc, std::nullopt, {}};
    for (const Targets& t : targets) {
        const auto* o = result.document.find(t.object);
        if (!o) return Error{"Object " + idString(t.object) + " no longer exists."};
        ProfileObject changed = asMeshGroup(*o);
        mesh::Mesh m = *changed.mesh;
        if (!edit(m, t)) continue;
        changed.mesh = std::make_shared<const mesh::Mesh>(std::move(m));
        auto step = replaceObject(result.document, std::move(changed));
        if (!step) return Error{step.error()};
        result.document = std::move(step).value().document;
        result.objectIds.push_back(t.object);
    }
    if (result.objectIds.empty()) return Error{unchanged};
    result.objectId = result.objectIds.back();
    return result;
}

/// The use of `edge` in `face`'s loops, if any.
const mesh::EdgeUse* useIn(const mesh::Face& face, mesh::EdgeId edge) {
    for (const auto& loop : face.loops) {
        if (const auto it = std::ranges::find(loop, edge, &mesh::EdgeUse::edge); it != loop.end()) return &*it;
    }
    return nullptr;
}

/// Turns a face over: every loop walks the other way and the normal flips.
/// The outer loop stays counter-clockwise (about the new normal), holes clockwise.
void reverseFace(mesh::Face& face) {
    for (auto& loop : face.loops) {
        std::ranges::reverse(loop);
        for (auto& use : loop) use.reversed = !use.reversed;
    }
    face.normal = -face.normal;
}

/// Soften/Smooth Edges on one mesh; `anyFold` is set when some edge borders two faces.
bool softenIn(mesh::Mesh& m, const Targets& t, const SoftenSettings& settings, bool& anyFold) {
    constexpr double kCoplanar = 1e-6; // radians: faces this close to parallel are coplanar
    bool changed = false;
    for (const auto id : t.edges) {
        const double angle = edgeFoldAngle(m, id);
        if (angle < 0.0) continue; // loose, border or non-manifold edge: left as it is
        anyFold = true;
        const bool soft = angle < kCoplanar ? settings.coplanar : angle <= settings.maxAngle + 1e-9;
        const bool smooth = soft && settings.smooth;
        mesh::Edge e = *m.edge(id);
        if (e.soft == soft && e.smooth == smooth) continue;
        e.soft = soft;
        e.smooth = smooth;
        m.insertEdge(id, e);
        changed = true;
    }
    return changed;
}

/// Orient Faces, one step: the face across `use` (an edge of face `id`), if
/// it is the only one there and not visited yet, is queued and turned to agree
/// with face `id`. Returns whether it turned.
bool alignAcross(mesh::Mesh& m, mesh::FaceId id, const mesh::EdgeUse& use, std::set<std::uint32_t>& visited,
                 std::deque<mesh::FaceId>& queue) {
    const auto across = m.facesOfEdge(use.edge);
    if (across.size() != 2) return false; // a border or a fin: no single neighbor to agree with
    const mesh::FaceId other = across[0] == id ? across[1] : across[0];
    if (other == id || !visited.insert(other.value).second) return false;
    queue.push_back(other);
    mesh::Face next = *m.face(other);
    const auto* theirs = useIn(next, use.edge);
    if (!theirs || theirs->reversed != use.reversed) return false;
    reverseFace(next);
    m.insertFace(other, next);
    return true;
}

/// Orient Faces from one seed: a breadth-first walk across edges shared by
/// exactly two faces. Returns whether any face turned.
bool orientFrom(mesh::Mesh& m, mesh::FaceId seed, std::set<std::uint32_t>& visited) {
    if (!visited.insert(seed.value).second) return false;
    bool changed = false;
    std::deque<mesh::FaceId> queue{seed};
    while (!queue.empty()) {
        const mesh::FaceId id = queue.front();
        queue.pop_front();
        const mesh::Face face = *m.face(id);
        for (const auto& loop : face.loops) {
            for (const auto& use : loop) changed = alignAcross(m, id, use, visited, queue) || changed;
        }
    }
    return changed;
}

/// Sets the hidden flag of the targets; returns whether anything changed.
bool hideIn(mesh::Mesh& m, const Targets& t, bool hidden) {
    bool changed = false;
    for (const auto id : t.edges) {
        mesh::Edge e = *m.edge(id);
        if (e.hidden == hidden) continue;
        e.hidden = hidden;
        m.insertEdge(id, e);
        changed = true;
    }
    for (const auto id : t.faces) {
        mesh::Face f = *m.face(id);
        if (f.hidden == hidden) continue;
        f.hidden = hidden;
        m.insertFace(id, f);
        changed = true;
    }
    return changed;
}

// ---- Flip Along ----------------------------------------------------------------------

/// The vertices of the targets' edges and faces.
std::set<std::uint32_t> verticesOf(const mesh::Mesh& m, const Targets& t) {
    std::set<std::uint32_t> out;
    for (const auto e : t.edges) {
        out.insert(m.edge(e)->v0.value);
        out.insert(m.edge(e)->v1.value);
    }
    for (const auto f : t.faces) {
        for (const auto& loop : m.face(f)->loops) {
            for (const auto v : m.loopVertices(loop)) out.insert(v.value);
        }
    }
    return out;
}

/// How many of a list of vertices are in `moved`: all, some or none.
struct Coverage {
    bool all = true;
    bool any = false;
    void add(bool moved) {
        any = any || moved;
        all = all && moved;
    }
};

Coverage faceCoverage(const mesh::Mesh& m, const mesh::Face& f, const std::set<std::uint32_t>& moved) {
    Coverage c;
    for (const auto& loop : f.loops) {
        for (const auto v : m.loopVertices(loop)) c.add(moved.contains(v.value));
    }
    return c;
}

Coverage curveCoverage(const mesh::Mesh& m, const mesh::Curve& curve, const std::set<std::uint32_t>& moved) {
    Coverage c;
    for (const auto e : curve.edges) {
        c.add(moved.contains(m.edge(e)->v0.value));
        c.add(moved.contains(m.edge(e)->v1.value));
    }
    return c;
}

/// A mirror across the plane `coordinate[axis] = at` (world space), seen from one object.
struct Mirror {
    int axis = 0;
    double at = 0.0;
    Vec3 translation{0.0};
    Mat3 r{1.0};  ///< object rotation
    Mat3 rt{1.0}; ///< its inverse
    Mat3 local{1.0}; ///< the reflection in object-local space (about the local origin)

    Mirror(const ProfileObject& o, int axis_, double at_) : axis(axis_), at(at_), translation(o.transform.translation) {
        r = rotationMatrix(o.transform);
        rt = glm::transpose(r);
        Mat3 reflect(1.0);
        reflect[axis][axis] = -1.0;
        local = rt * reflect * r;
    }
    [[nodiscard]] Vec3 point(const Vec3& q) const {
        Vec3 p = translation + r * q;
        p[axis] = 2.0 * at - p[axis];
        return rt * (p - translation);
    }
};

/// Re-fits the faces around the moved vertices; a face mirrored whole turns
/// over, so it keeps facing the same side of the shape.
void refitFaces(mesh::Mesh& m, const std::set<std::uint32_t>& moved) {
    std::vector<std::pair<std::uint32_t, mesh::Face>> faces(m.faces().begin(), m.faces().end());
    for (auto& [id, f] : faces) {
        const Coverage c = faceCoverage(m, f, moved);
        if (!c.any) continue;
        if (c.all) reverseFace(f);
        if (const Vec3 n = mesh::loopNormal(m.loopPositions(f.loops[0])); glm::length(n) > 0.0) f.normal = n;
        m.insertFace(mesh::FaceId{id}, f);
    }
}

/// A curve mirrored whole stays a curve; one cut in two becomes plain segments.
void mirrorCurves(mesh::Mesh& m, const std::set<std::uint32_t>& moved, const Mirror& mirror) {
    std::vector<std::pair<std::uint32_t, mesh::Curve>> curves(m.curves().begin(), m.curves().end());
    for (auto& [id, c] : curves) {
        const Coverage cover = curveCoverage(m, c, moved);
        if (!cover.any) continue;
        if (!cover.all) {
            m.removeCurve(mesh::CurveId{id});
            continue;
        }
        c.center = mirror.point(c.center);
        c.xAxis = mirror.local * c.xAxis;
        c.normal = -(mirror.local * c.normal); // a mirror reverses turning, so the same start and sweep need the opposite normal
        m.insertCurve(mesh::CurveId{id}, c);
    }
}

/// Mirrors the targets of one mesh. Returns false (and sets `bent`) when the
/// faces around them would no longer be flat.
bool flipIn(mesh::Mesh& m, const Targets& t, const Mirror& mirror, bool& bent) {
    const auto moved = verticesOf(m, t);
    if (moved.empty()) return false;
    for (const auto v : moved) m.insertVertex(mesh::VertexId{v}, mirror.point(m.vertex(mesh::VertexId{v})->position));
    refitFaces(m, moved);
    mirrorCurves(m, moved, mirror);
    if (mesh::validateMesh(m, "mesh")) {
        bent = true; // a face around the flipped part is no longer flat
        return false;
    }
    return true;
}

} // namespace

CleanupScope cleanupScope(const SelectionSet& selection) {
    CleanupScope scope;
    for (const auto& ref : selection.elements()) {
        if (ref.kind != ElementKind::Vertex) scope.elements.push_back(ref);
    }
    for (const ObjectId id : selection.objects()) {
        if (!selection.hasElements(id)) scope.objects.push_back(id);
    }
    return scope;
}

double edgeFoldAngle(const mesh::Mesh& m, mesh::EdgeId edge) {
    const auto faces = m.facesOfEdge(edge);
    if (faces.size() != 2) return -1.0;
    const auto* a = m.face(faces[0]);
    const auto* b = m.face(faces[1]);
    const auto* ua = useIn(*a, edge);
    const auto* ub = useIn(*b, edge);
    if (!ua || !ub) return -1.0;
    // Faces that agree about their front side walk a shared edge in opposite
    // directions; when they walk it the same way, one of them is back to front.
    const Vec3 nb = ua->reversed == ub->reversed ? -b->normal : b->normal;
    return std::acos(std::clamp(glm::dot(a->normal, nb), -1.0, 1.0));
}

Expected<OpResult> softenEdges(const Document& doc, const CleanupScope& scope, const SoftenSettings& settings) {
    if (scope.empty()) return Error{"Select the edges, faces or objects to soften first."};
    if (!std::isfinite(settings.maxAngle) || settings.maxAngle < 0.0) return Error{"The angle must be between 0 and 180 degrees."};
    bool anyFold = false;
    const auto edit = [&settings, &anyFold](mesh::Mesh& m, const Targets& t) { return softenIn(m, t, settings, anyFold); };
    auto result = editMeshes(doc, resolve(doc, scope, true), edit, "");
    if (!result && !anyFold) return Error{"Soften works on edges between two faces, and none is selected."};
    if (!result && result.error().empty()) return Error{"The selected edges are already softened like this."};
    return result;
}

Expected<OpResult> reverseFaces(const Document& doc, const CleanupScope& scope) {
    const auto edit = [](mesh::Mesh& m, const Targets& t) {
        for (const auto id : t.faces) {
            mesh::Face f = *m.face(id);
            reverseFace(f);
            m.insertFace(id, f);
        }
        return !t.faces.empty();
    };
    return editMeshes(doc, resolve(doc, scope, false), edit, "Select the faces to reverse first.");
}

Expected<OpResult> orientFaces(const Document& doc, const CleanupScope& scope) {
    // Only selected faces are seeds: an object selected whole has no face to agree with.
    CleanupScope faces;
    std::ranges::copy_if(scope.elements, std::back_inserter(faces.elements),
                         [](const ElementRef& ref) { return ref.kind == ElementKind::Face; });
    if (faces.empty()) return Error{"Select a face first: Orient Faces turns the faces around it to match it."};
    const auto edit = [](mesh::Mesh& m, const Targets& t) {
        bool changed = false;
        std::set<std::uint32_t> visited;
        for (const auto seed : t.faces) changed = orientFrom(m, seed, visited) || changed;
        return changed;
    };
    return editMeshes(doc, resolve(doc, faces, false), edit, "The connected faces already face the same way as the selected one.");
}

Expected<OpResult> setHidden(const Document& doc, const CleanupScope& scope, bool hidden) {
    if (scope.empty()) return Error{hidden ? "Select the edges or faces to hide first." : "Select the hidden edges or faces to unhide first."};
    const auto edit = [hidden](mesh::Mesh& m, const Targets& t) { return hideIn(m, t, hidden); };
    return editMeshes(doc, resolve(doc, scope, false), edit,
                      hidden ? "The selection is already hidden." : "Nothing selected is hidden.");
}

bool anyHidden(const Document& doc) {
    return std::ranges::any_of(doc.objects, [](const ObjectPtr& o) {
        if (!o->isMeshGroup()) return false; // a rectangle or box has nothing hidden
        return std::ranges::any_of(o->mesh->edges(), [](const auto& e) { return e.second.hidden; }) ||
               std::ranges::any_of(o->mesh->faces(), [](const auto& f) { return f.second.hidden; });
    });
}

Expected<OpResult> unhideAll(const Document& doc) {
    CleanupScope scope;
    for (const auto& o : doc.objects) {
        if (o->isMeshGroup()) scope.objects.push_back(o->id);
    }
    auto result = setHidden(doc, scope, false);
    if (!result) return Error{"Nothing is hidden."};
    return result;
}

Expected<OpResult> flipAlong(const Document& doc, const CleanupScope& scope, int axis) {
    if (axis < 0 || axis > 2) return Error{"Flip along the red, green or blue axis."};
    const auto targets = resolve(doc, scope, false);
    if (targets.empty()) return Error{"Select the geometry or objects to flip first."};

    // The mirror plane goes through the centre of the flipped vertices' world bounds.
    Bounds bounds;
    for (const Targets& t : targets) {
        const auto* o = doc.find(t.object);
        const auto m = mesh::objectMesh(*o);
        for (const auto v : verticesOf(*m, t)) bounds.extend(localToWorld(*o, m->vertex(mesh::VertexId{v})->position));
    }
    const double at = bounds.center()[axis];

    bool bent = false;
    const auto edit = [&doc, axis, at, &bent](mesh::Mesh& m, const Targets& t) {
        return flipIn(m, t, Mirror(*doc.find(t.object), axis, at), bent);
    };
    auto result = editMeshes(doc, targets, edit, "Select the geometry or objects to flip first.");
    if (bent) {
        return Error{"Flipping only part of the geometry would bend the faces attached to it. Select all of it (triple-click) "
                     "or the whole object."};
    }
    return result;
}

} // namespace pf
