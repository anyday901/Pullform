#include "core/commands/cleanup.hpp"

#include "core/commands/object_edit.hpp"
#include "core/geometry/mesh.hpp"
#include "core/geometry/solid.hpp"

#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>

#include <algorithm>
#include <cmath>
#include <deque>
#include <functional>
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
    auto it = std::ranges::find_if(all, [id](const Targets& t) { return t.object == id; });
    if (it != all.end()) return *it;
    all.push_back(Targets{id, {}, {}});
    return all.back();
}

/// Resolves a scope against the document, objects in first-seen order. With
/// `faceEdges`, a selected face brings its bounding edges along.
std::vector<Targets> resolve(const Document& doc, const CleanupScope& scope, bool faceEdges) {
    std::vector<Targets> all;
    for (const ObjectId id : scope.objects) {
        const auto* o = doc.find(id);
        if (!o) continue;
        const auto m = mesh::objectMesh(*o);
        Targets& t = targetsFor(all, id);
        for (const auto& [eid, e] : m->edges()) addUnique(t.edges, mesh::EdgeId{eid});
        for (const auto& [fid, f] : m->faces()) addUnique(t.faces, mesh::FaceId{fid});
    }
    for (const auto& ref : scope.elements) {
        const auto* o = doc.find(ref.object);
        if (!o) continue;
        const auto m = mesh::objectMesh(*o);
        if (ref.kind == ElementKind::Edge && m->edge(mesh::EdgeId{ref.id})) {
            addUnique(targetsFor(all, ref.object).edges, mesh::EdgeId{ref.id});
        } else if (ref.kind == ElementKind::Face && m->face(mesh::FaceId{ref.id})) {
            Targets& t = targetsFor(all, ref.object);
            addUnique(t.faces, mesh::FaceId{ref.id});
            if (faceEdges) {
                for (const auto e : m->edgesOfFace(mesh::FaceId{ref.id})) addUnique(t.edges, e);
            }
        }
    }
    return all;
}

/// Runs `edit` on the mesh of every target object; objects it reports as
/// changed are replaced (validated), the rest are left as they are. Fails
/// with `unchanged` when nothing changed.
Expected<OpResult> editMeshes(const Document& doc, const std::vector<Targets>& targets,
                              const std::function<bool(mesh::Mesh&, const Targets&)>& edit, const std::string& unchanged) {
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
        for (const auto& use : loop) {
            if (use.edge == edge) return &use;
        }
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
    constexpr double kCoplanar = 1e-6; // radians: faces this close to parallel are coplanar
    bool anyFold = false;
    const auto edit = [&](mesh::Mesh& m, const Targets& t) {
        bool changed = false;
        for (const auto id : t.edges) {
            const double angle = edgeFoldAngle(m, id);
            if (angle < 0.0) continue; // loose, border or non-manifold edge: left as it is
            anyFold = true;
            const bool soft = angle < kCoplanar ? settings.coplanar : angle <= settings.maxAngle + 1e-9;
            mesh::Edge e = *m.edge(id);
            if (e.soft == soft && e.smooth == (soft && settings.smooth)) continue;
            e.soft = soft;
            e.smooth = soft && settings.smooth;
            m.insertEdge(id, e);
            changed = true;
        }
        return changed;
    };
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
    for (const auto& ref : scope.elements) {
        if (ref.kind == ElementKind::Face) faces.elements.push_back(ref);
    }
    if (faces.empty()) return Error{"Select a face first: Orient Faces turns the faces around it to match it."};
    const auto edit = [](mesh::Mesh& m, const Targets& t) {
        bool changed = false;
        std::set<std::uint32_t> visited;
        for (const auto seed : t.faces) {
            if (!visited.insert(seed.value).second) continue;
            std::deque<mesh::FaceId> queue{seed};
            while (!queue.empty()) {
                const mesh::FaceId id = queue.front();
                queue.pop_front();
                const mesh::Face face = *m.face(id);
                for (const auto& loop : face.loops) {
                    for (const auto& use : loop) {
                        const auto across = m.facesOfEdge(use.edge);
                        if (across.size() != 2) continue; // a border or a fin: no single neighbor to agree with
                        const mesh::FaceId other = across[0] == id ? across[1] : across[0];
                        if (other == id || !visited.insert(other.value).second) continue;
                        mesh::Face next = *m.face(other);
                        if (const auto* theirs = useIn(next, use.edge); theirs && theirs->reversed == use.reversed) {
                            reverseFace(next);
                            m.insertFace(other, next);
                            changed = true;
                        }
                        queue.push_back(other);
                    }
                }
            }
        }
        return changed;
    };
    return editMeshes(doc, resolve(doc, faces, false), edit, "The connected faces already face the same way as the selected one.");
}

Expected<OpResult> setHidden(const Document& doc, const CleanupScope& scope, bool hidden) {
    if (scope.empty()) return Error{hidden ? "Select the edges or faces to hide first." : "Select the hidden edges or faces to unhide first."};
    const auto edit = [hidden](mesh::Mesh& m, const Targets& t) {
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
    };
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

    // The vertices to mirror, per object, and the centre of their world bounds.
    const auto verticesOf = [](const mesh::Mesh& m, const Targets& t) {
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
    };
    Bounds bounds;
    for (const Targets& t : targets) {
        const auto* o = doc.find(t.object);
        const auto m = mesh::objectMesh(*o);
        for (const auto v : verticesOf(*m, t)) bounds.extend(localToWorld(*o, m->vertex(mesh::VertexId{v})->position));
    }
    const double mirror = bounds.center()[axis];

    bool bent = false;
    const auto edit = [&](mesh::Mesh& m, const Targets& t) {
        const ProfileObject& o = *doc.find(t.object);
        const Mat3 r = rotationMatrix(o.transform);
        const Mat3 rt = glm::transpose(r);
        Mat3 reflect(1.0);
        reflect[axis][axis] = -1.0;
        const Mat3 local = rt * reflect * r; // the reflection in object-local space (about the local origin)
        const auto flipPoint = [&](const Vec3& q) {
            Vec3 p = o.transform.translation + r * q;
            p[axis] = 2.0 * mirror - p[axis];
            return rt * (p - o.transform.translation);
        };
        const auto moved = verticesOf(m, t);
        if (moved.empty()) return false;
        for (const auto v : moved) m.insertVertex(mesh::VertexId{v}, flipPoint(m.vertex(mesh::VertexId{v})->position));

        // A face mirrored whole turns over, so it keeps facing the same side of
        // the shape; any face touching a moved vertex gets its plane re-fitted.
        std::vector<std::pair<std::uint32_t, mesh::Face>> faces(m.faces().begin(), m.faces().end());
        for (auto& [id, f] : faces) {
            bool all = true, any = false;
            for (const auto& loop : f.loops) {
                for (const auto v : m.loopVertices(loop)) {
                    if (moved.contains(v.value)) any = true;
                    else all = false;
                }
            }
            if (!any) continue;
            if (all) reverseFace(f);
            if (const Vec3 n = mesh::loopNormal(m.loopPositions(f.loops[0])); glm::length(n) > 0.0) f.normal = n;
            m.insertFace(mesh::FaceId{id}, f);
        }
        // A curve mirrored whole stays a curve; one cut in two becomes plain segments.
        std::vector<std::pair<std::uint32_t, mesh::Curve>> curves(m.curves().begin(), m.curves().end());
        for (auto& [id, c] : curves) {
            bool all = true, any = false;
            for (const auto e : c.edges) {
                for (const auto v : {m.edge(e)->v0, m.edge(e)->v1}) {
                    if (moved.contains(v.value)) any = true;
                    else all = false;
                }
            }
            if (!any) continue;
            if (!all) {
                m.removeCurve(mesh::CurveId{id});
                continue;
            }
            c.center = flipPoint(c.center);
            c.xAxis = local * c.xAxis;
            c.normal = -(local * c.normal); // a mirror reverses turning, so the same start and sweep need the opposite normal
            m.insertCurve(mesh::CurveId{id}, c);
        }
        if (mesh::validateMesh(m, "mesh")) {
            bent = true; // a face around the flipped part is no longer flat
            return false;
        }
        return true;
    };
    auto result = editMeshes(doc, targets, edit, "Select the geometry or objects to flip first.");
    if (bent) {
        return Error{"Flipping only part of the geometry would bend the faces attached to it. Select all of it (triple-click) "
                     "or the whole object."};
    }
    return result;
}

} // namespace pf
