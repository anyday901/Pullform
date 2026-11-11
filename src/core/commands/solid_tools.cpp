#include "core/commands/solid_tools.hpp"

#include "core/document/validate.hpp"
#include "core/geometry/measure.hpp"
#include "core/geometry/mesh.hpp"
#include "core/geometry/solid_boolean.hpp"

#include <glm/geometric.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace pf {

namespace {

double volumeOf(const mesh::Mesh& m) {
    const MeshData data = mesh::solidMeshData(m);
    double v = 0.0;
    for (std::size_t t = 0; t + 2 < data.indices.size(); t += 3) {
        const Vec3& a = data.positions[data.indices[t]];
        const Vec3& b = data.positions[data.indices[t + 1]];
        const Vec3& c = data.positions[data.indices[t + 2]];
        v += glm::dot(a, glm::cross(b, c)) / 6.0;
    }
    return v;
}

/// `object`'s mesh in the local frame of `frame`.
mesh::Mesh meshIn(const ProfileObject& frame, const ProfileObject& object) {
    const Mat3 toFrame = glm::transpose(rotationMatrix(frame.transform));
    return mesh::transformedMesh(*mesh::objectMesh(object), toFrame * rotationMatrix(object.transform),
                                 toFrame * (object.transform.translation - frame.transform.translation));
}

std::string displayName(const ProfileObject& o) { return o.name.empty() ? idString(o.id) : o.name; }

/// `base` holding `m` as a mesh group (same id, name and placement). Default
/// names of rectangles, boxes and the like follow it to "Group <n>", as other
/// edits that turn a profile into a mesh do.
ProfileObject withMesh(const ProfileObject& base, mesh::Mesh m) {
    ProfileObject changed = base;
    changed.mesh = std::make_shared<const mesh::Mesh>(std::move(m));
    changed.profile.boundary.clear();
    changed.profile.kind = ProfileKind::Rectangle;
    changed.extrusion.reset();
    constexpr std::array<std::string_view, 6> kDefault{"Rectangle ", "Box ", "Circle ", "Cylinder ", "Polygon ", "Prism "};
    for (const auto prefix : kDefault) {
        if (changed.name == std::string(prefix) + std::to_string(base.id.value)) {
            changed.name = "Group " + std::to_string(base.id.value);
            break;
        }
    }
    return changed;
}

Expected<mesh::Mesh> run(const mesh::Mesh& a, const mesh::Mesh& b, mesh::BooleanOp op) { return mesh::booleanSolids(a, b, op); }

/// Builds the result document: `replaced` objects swapped in place, `removed`
/// objects dropped, `added` appended; dimensions on any of them are dropped.
Expected<OpResult> finish(const Document& doc, const std::vector<ProfileObject>& replaced, const std::vector<ObjectId>& removed,
                          const std::vector<ProfileObject>& added, ObjectId main) {
    OpResult result{doc, main, {}};
    Document& out = result.document;
    std::vector<ObjectId> touched = removed;
    for (const auto& o : replaced) {
        if (auto e = validateObject(o, idString(o.id))) return Error{"The result is not valid: " + *e};
        const auto index = out.indexOf(o.id);
        if (index < 0) return Error{"Object " + idString(o.id) + " no longer exists."};
        out.objects[static_cast<std::size_t>(index)] = std::make_shared<const ProfileObject>(o);
        touched.push_back(o.id);
        result.objectIds.push_back(o.id);
    }
    for (const ObjectId id : removed) std::erase_if(out.objects, [id](const ObjectPtr& o) { return o->id == id; });
    for (const auto& o : added) {
        if (auto e = validateObject(o, idString(o.id))) return Error{"The result is not valid: " + *e};
        out.objects.push_back(std::make_shared<const ProfileObject>(o));
        out.nextId = std::max(out.nextId, o.id.value + 1);
        result.objectIds.push_back(o.id);
    }
    // A dimension names corners by number, and the corners were renumbered.
    std::erase_if(out.dimensions, [&touched](const Dimension& d) {
        return std::ranges::any_of(touched, [&d](ObjectId id) { return anchorUses(d.a, id) || anchorUses(d.b, id); });
    });
    return result;
}

} // namespace

std::string_view solidToolName(SolidTool tool) {
    switch (tool) {
    case SolidTool::Union: return "Union";
    case SolidTool::Subtract: return "Subtract";
    case SolidTool::Intersect: return "Intersect";
    case SolidTool::Trim: return "Trim";
    case SolidTool::Split: return "Split";
    case SolidTool::OuterShell: return "Outer Shell";
    }
    return "Solid tool";
}

std::string_view solidToolHelp(SolidTool tool) {
    switch (tool) {
    case SolidTool::Union: return "Joins the selected solids into one solid.";
    case SolidTool::Subtract: return "Cuts the first selected solid out of the second, and removes the first.";
    case SolidTool::Intersect: return "Keeps only the part the selected solids share.";
    case SolidTool::Trim: return "Cuts the first selected solid out of the second, and keeps the first.";
    case SolidTool::Split: return "Cuts both solids where they overlap, leaving the two remainders and the overlap.";
    case SolidTool::OuterShell: return "Joins the selected solids and removes everything inside the outer surface.";
    }
    return "";
}

bool solidToolAccepts(SolidTool tool, std::size_t count) {
    switch (tool) {
    case SolidTool::Subtract:
    case SolidTool::Trim:
    case SolidTool::Split: return count == 2;
    case SolidTool::Union:
    case SolidTool::Intersect: return count >= 2;
    case SolidTool::OuterShell: return count >= 1;
    }
    return false;
}

std::optional<std::string> solidObjectProblem(const ProfileObject& object) {
    if (!object.isMeshGroup() && !object.extrusion) return std::string{"it is a flat face; push/pull it into a solid first"};
    return mesh::solidProblem(*mesh::objectMesh(object));
}

Expected<OpResult> applySolidTool(const Document& doc, SolidTool tool, const std::vector<ObjectId>& ids) {
    const std::string name(solidToolName(tool));
    if (!solidToolAccepts(tool, ids.size())) {
        switch (tool) {
        case SolidTool::Subtract:
        case SolidTool::Trim:
        case SolidTool::Split: return Error{name + " needs exactly two solids selected."};
        case SolidTool::OuterShell: return Error{"Select the solids for Outer Shell."};
        default: return Error{name + " needs two or more solids selected."};
        }
    }
    std::vector<const ProfileObject*> objects;
    for (const ObjectId id : ids) {
        const ProfileObject* o = doc.find(id);
        if (!o) return Error{"Object " + idString(id) + " no longer exists."};
        if (std::ranges::find(objects, o) != objects.end()) return Error{"Object " + idString(id) + " is listed twice."};
        if (auto p = solidObjectProblem(*o)) return Error{displayName(*o) + " is not a solid: " + *p + "."};
        objects.push_back(o);
    }

    switch (tool) {
    case SolidTool::Union:
    case SolidTool::Intersect:
    case SolidTool::OuterShell: {
        const ProfileObject& base = *objects.front();
        mesh::Mesh acc = *mesh::objectMesh(base);
        const auto op = tool == SolidTool::Intersect ? mesh::BooleanOp::Intersect : mesh::BooleanOp::Union;
        // An operand that cannot be combined yet (two boxes touching along an edge)
        // may combine once a later one has joined them, so it waits for another pass.
        std::vector<const ProfileObject*> pending(objects.begin() + 1, objects.end());
        while (!pending.empty()) {
            const std::size_t before = pending.size();
            std::string failure;
            for (auto it = pending.begin(); it != pending.end();) {
                auto r = run(acc, meshIn(base, **it), op);
                if (!r) {
                    failure = r.error();
                    ++it;
                    continue;
                }
                acc = std::move(r).value();
                if (acc.faces().empty()) return Error{"The solids do not overlap, so they have nothing in common."};
                it = pending.erase(it);
            }
            if (pending.size() == before) return Error{name + " failed. " + failure};
        }
        if (tool == SolidTool::OuterShell) {
            const std::size_t before = acc.faces().size();
            acc = mesh::outerShells(acc);
            if (objects.size() == 1 && acc.faces().size() == before) {
                return Error{displayName(base) + " has nothing inside its outer shell to remove."};
            }
        }
        std::vector<ObjectId> removed;
        for (std::size_t k = 1; k < objects.size(); ++k) removed.push_back(objects[k]->id);
        return finish(doc, {withMesh(base, std::move(acc))}, removed, {}, base.id);
    }
    case SolidTool::Subtract:
    case SolidTool::Trim: {
        const ProfileObject& cutter = *objects[0];
        const ProfileObject& target = *objects[1];
        const mesh::Mesh targetMesh = *mesh::objectMesh(target);
        auto r = run(targetMesh, meshIn(target, cutter), mesh::BooleanOp::Subtract);
        if (!r) return Error{name + " failed. " + r.error()};
        if (r->faces().empty()) return Error{name + " would leave nothing of " + displayName(target) + "."};
        const double v0 = volumeOf(targetMesh);
        if (std::abs(volumeOf(*r) - v0) <= 1e-9 * std::max(v0, 1e-12)) {
            return Error{displayName(cutter) + " does not overlap " + displayName(target) + ", so there is nothing to cut."};
        }
        std::vector<ObjectId> removed;
        if (tool == SolidTool::Subtract) removed.push_back(cutter.id);
        auto result = finish(doc, {withMesh(target, std::move(r).value())}, removed, {}, target.id);
        if (result && tool == SolidTool::Trim) {
            OpResult out = std::move(result).value();
            out.objectIds.insert(out.objectIds.begin(), cutter.id);
            return out;
        }
        return result;
    }
    case SolidTool::Split: {
        const ProfileObject& a = *objects[0];
        const ProfileObject& b = *objects[1];
        const mesh::Mesh ma = *mesh::objectMesh(a);
        const mesh::Mesh mb = *mesh::objectMesh(b);
        const mesh::Mesh bInA = meshIn(a, b);
        auto common = run(ma, bInA, mesh::BooleanOp::Intersect);
        if (!common) return Error{name + " failed. " + common.error()};
        if (common->faces().empty()) return Error{"The solids do not overlap, so there is nothing to split."};
        auto restA = run(ma, bInA, mesh::BooleanOp::Subtract);
        if (!restA) return Error{name + " failed. " + restA.error()};
        auto restB = run(mb, meshIn(b, a), mesh::BooleanOp::Subtract);
        if (!restB) return Error{name + " failed. " + restB.error()};

        std::vector<ProfileObject> replaced;
        std::vector<ObjectId> removed;
        for (auto* rest : {&restA, &restB}) {
            const ProfileObject& o = rest == &restA ? a : b;
            if (rest->value().faces().empty()) removed.push_back(o.id);
            else replaced.push_back(withMesh(o, std::move(*rest).value()));
        }
        ProfileObject piece = withMesh(a, std::move(common).value());
        piece.id = ObjectId{doc.nextId};
        piece.name = "Group " + std::to_string(doc.nextId);
        return finish(doc, replaced, removed, {piece}, piece.id);
    }
    }
    return Error{"Unknown solid tool."};
}

} // namespace pf
