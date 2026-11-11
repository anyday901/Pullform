#include "core/geometry/solid.hpp"

#include "core/geometry/mesh.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <map>
#include <utility>

namespace pf {

namespace {

/// Faces of a mesh group: each face's outer loop in world space (holes are in
/// the mesh; buildMesh triangulates them).
std::vector<FacePolygon> meshGroupFaces(const ProfileObject& o) {
    std::vector<FacePolygon> faces;
    const Mat3 r = rotationMatrix(o.transform);
    for (const auto& [id, f] : o.mesh->faces()) {
        FacePolygon poly{FaceId::meshFace(id), {}, glm::normalize(r * f.normal)};
        for (const auto& p : o.mesh->loopPositions(f.loops[0])) poly.loop.push_back(localToWorld(o, p));
        faces.push_back(std::move(poly));
    }
    return faces;
}

/// Normal of `face` at `vertex`: the face normal, averaged with the faces
/// across each smooth edge of the face that meets the vertex (a cylinder's
/// sides), so smoothed surfaces stay smooth once a group holds a mesh.
Vec3 shadedNormal(const mesh::Mesh& m, std::uint32_t faceId, const mesh::Face& face, mesh::VertexId vertex) {
    Vec3 sum = face.normal;
    for (const auto& loop : face.loops) {
        for (const auto& use : loop) {
            const auto* e = m.edge(use.edge);
            if (!e || !e->smooth || (e->v0 != vertex && e->v1 != vertex)) continue;
            for (const auto other : m.facesOfEdge(use.edge)) {
                if (other.value == faceId) continue;
                const Vec3 n = m.face(other)->normal;
                sum += glm::dot(n, face.normal) < 0.0 ? -n : n;
            }
        }
    }
    return glm::normalize(sum);
}

MeshData meshGroupMesh(const ProfileObject& o) {
    const mesh::Mesh& m = *o.mesh;
    const Mat3 r = rotationMatrix(o.transform);
    const bool anySmooth = std::ranges::any_of(m.edges(), [](const auto& entry) { return entry.second.smooth; });
    MeshData data;
    for (const auto& [id, f] : m.faces()) {
        const FaceId faceId = FaceId::meshFace(id);
        const Vec3 n = glm::normalize(r * f.normal);
        const auto tris = mesh::triangulateFace(m, mesh::FaceId{id});
        const auto first = static_cast<std::uint32_t>(data.indices.size());
        // Two-sided like a planar profile: front set, then the reversed back set.
        for (const double side : {1.0, -1.0}) {
            for (const auto& t : tris) {
                const auto base = static_cast<std::uint32_t>(data.positions.size());
                for (int k = 0; k < 3; ++k) {
                    const auto v = side > 0 ? t[static_cast<std::size_t>(k)] : t[static_cast<std::size_t>(2 - k)];
                    data.positions.push_back(localToWorld(o, m.vertex(v)->position));
                    data.normals.push_back((anySmooth ? glm::normalize(r * shadedNormal(m, id, f, v)) : n) * side);
                }
                data.indices.insert(data.indices.end(), {base, base + 1, base + 2});
                data.triangleFaces.push_back(faceId);
            }
        }
        data.faces.push_back({faceId, first, static_cast<std::uint32_t>(data.indices.size()) - first});
    }
    for (const auto& [id, e] : m.edges()) {
        data.edges.push_back(localToWorld(o, m.vertex(e.v0)->position));
        data.edges.push_back(localToWorld(o, m.vertex(e.v1)->position));
    }
    return data;
}

} // namespace

std::vector<FacePolygon> buildFaces(const ProfileObject& o, std::optional<double> heightOverride) {
    if (o.mesh) return meshGroupFaces(o);
    const auto& boundary = o.profile.boundary;
    const Vec3 n = profileNormalWorld(o);
    std::optional<double> height = heightOverride;
    if (!height && o.extrusion) height = o.extrusion->height;

    std::vector<Vec3> bottom;
    bottom.reserve(boundary.size());
    for (const auto& p : boundary) bottom.push_back(profileToWorld(o, p, 0.0));
    if (!height) return {FacePolygon{FaceId::profile(), bottom, n}};

    std::vector<Vec3> top;
    top.reserve(boundary.size());
    for (const auto& p : boundary) top.push_back(profileToWorld(o, p, *height));

    std::vector<FacePolygon> faces;
    faces.push_back({FaceId::base(), {bottom.rbegin(), bottom.rend()}, -n});
    faces.push_back({FaceId::cap(), top, n});
    for (std::size_t i = 0; i < boundary.size(); ++i) {
        const std::size_t j = (i + 1) % boundary.size();
        // CCW boundary: the interior is left of a->b, so (b - a) x n points outward.
        const Vec3 outward = glm::normalize(glm::cross(bottom[j] - bottom[i], n));
        faces.push_back({FaceId::sideAt(static_cast<int>(i)), {bottom[i], bottom[j], top[j], top[i]}, outward});
    }
    return faces;
}

MeshData buildMesh(const ProfileObject& o, std::optional<double> heightOverride) {
    if (o.mesh) return meshGroupMesh(o);
    const auto faces = buildFaces(o, heightOverride);
    MeshData mesh;
    mesh.closed = faces.size() > 1;

    // A cylinder's sides are shaded smooth: each corner's normal points away
    // from the axis, so the facets read as one round surface.
    const bool smoothSides = o.profile.kind == ProfileKind::Circle && faces.size() > 1;
    const Vec3 axisPoint = profileToWorld(o, Vec2{0.0}, 0.0);
    const Vec3 axis = profileNormalWorld(o);
    const auto emit = [&](const FaceId& id, const std::vector<Vec3>& loop, const Vec3& normal) {
        const auto base = static_cast<std::uint32_t>(mesh.positions.size());
        for (const auto& p : loop) {
            mesh.positions.push_back(p);
            Vec3 shaded = normal;
            if (smoothSides && id.kind == FaceKind::Side) {
                const Vec3 w = p - axisPoint;
                if (const Vec3 radial = w - axis * glm::dot(w, axis); glm::length(radial) > 0.0) shaded = glm::normalize(radial);
            }
            mesh.normals.push_back(shaded);
        }
        for (std::uint32_t i = 1; i + 1 < loop.size(); ++i) {
            mesh.indices.insert(mesh.indices.end(), {base, base + i, base + i + 1});
            mesh.triangleFaces.push_back(id);
        }
    };

    for (const auto& face : faces) {
        const auto first = static_cast<std::uint32_t>(mesh.indices.size());
        emit(face.id, face.loop, face.normal);
        if (!mesh.closed) {
            // Back side of a planar sheet: reversed loop and normal, same logical face.
            emit(face.id, {face.loop.rbegin(), face.loop.rend()}, -face.normal);
        }
        mesh.faces.push_back({face.id, first, static_cast<std::uint32_t>(mesh.indices.size()) - first});
    }

    // Boundary edges from face loops, each undirected edge once. Shared corners are
    // computed once per object (bottom/top arrays), so exact comparison is reliable.
    using Key = std::array<double, 6>;
    std::vector<Key> seen;
    for (const auto& face : faces) {
        for (std::size_t i = 0; i < face.loop.size(); ++i) {
            Vec3 a = face.loop[i];
            Vec3 b = face.loop[(i + 1) % face.loop.size()];
            Key k1{a.x, a.y, a.z, b.x, b.y, b.z};
            Key k2{b.x, b.y, b.z, a.x, a.y, a.z};
            const Key key = std::min(k1, k2);
            if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
            seen.push_back(key);
            mesh.edges.push_back(a);
            mesh.edges.push_back(b);
        }
    }
    return mesh;
}

SolidCheck checkSolid(const MeshData& mesh) {
    SolidCheck result;
    std::map<std::array<double, 3>, std::size_t> weld;
    std::vector<std::size_t> vertexId(mesh.positions.size());
    for (std::size_t i = 0; i < mesh.positions.size(); ++i) {
        const auto& p = mesh.positions[i];
        vertexId[i] = weld.emplace(std::array<double, 3>{p.x, p.y, p.z}, weld.size()).first->second;
    }

    std::map<std::pair<std::size_t, std::size_t>, int> directed;
    bool normalsOk = true;
    double volume = 0.0;
    for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        const std::uint32_t ia = mesh.indices[t], ib = mesh.indices[t + 1], ic = mesh.indices[t + 2];
        const Vec3 &a = mesh.positions[ia], &b = mesh.positions[ib], &c = mesh.positions[ic];
        const Vec3 cr = glm::cross(b - a, c - a);
        const double len = glm::length(cr);
        // Flat faces store the face normal; smooth ones (a cylinder's sides) tilt
        // it toward their neighbours, so compare the triangle's mean normal loosely.
        const Vec3 shaded = mesh.normals[ia] + mesh.normals[ib] + mesh.normals[ic];
        if (len <= 0.0 || glm::length(shaded) <= 0.0 || glm::dot(cr / len, glm::normalize(shaded)) < 0.5) normalsOk = false;
        volume += glm::dot(a, glm::cross(b, c)) / 6.0;
        const std::size_t va = vertexId[ia], vb = vertexId[ib], vc = vertexId[ic];
        ++directed[{va, vb}];
        ++directed[{vb, vc}];
        ++directed[{vc, va}];
    }

    bool closed = true, oriented = true;
    for (const auto& [edge, count] : directed) {
        if (count > 1) oriented = false;
        const auto twin = directed.find({edge.second, edge.first});
        if (twin == directed.end() || twin->second != 1) closed = false;
    }
    result.closed = closed && !directed.empty();
    result.consistentlyOriented = oriented && !directed.empty();
    result.normalsMatchWinding = normalsOk;
    result.signedVolume = volume;
    if (!result.closed) result.problem = "surface is not closed";
    else if (!result.consistentlyOriented) result.problem = "faces are not consistently oriented";
    else if (!result.normalsMatchWinding) result.problem = "stored normals disagree with triangle winding";
    else if (!(volume > 0.0)) result.problem = "faces point inward (non-positive volume)";
    return result;
}

void Bounds::extend(const Vec3& p) {
    if (!valid) {
        min = max = p;
        valid = true;
        return;
    }
    min = glm::min(min, p);
    max = glm::max(max, p);
}

void Bounds::extend(const Bounds& b) {
    if (!b.valid) return;
    extend(b.min);
    extend(b.max);
}

Bounds objectBounds(const ProfileObject& o) {
    Bounds b;
    if (o.mesh) {
        for (const auto& [id, v] : o.mesh->vertices()) b.extend(localToWorld(o, v.position));
        return b;
    }
    for (const auto& face : buildFaces(o)) {
        for (const auto& p : face.loop) b.extend(p);
    }
    return b;
}

Bounds documentBounds(const Document& doc) {
    Bounds b;
    for (const auto& o : doc.objects) b.extend(objectBounds(*o));
    return b;
}

} // namespace pf
