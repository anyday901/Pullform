#include "core/geometry/topology.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace pf {

std::optional<std::uint32_t> ElementTopology::faceFor(const FaceId& logical) const {
    for (std::uint32_t i = 0; i < faces.size(); ++i) {
        if (faces[i].logical == logical) return i;
    }
    return std::nullopt;
}

std::optional<std::uint32_t> ElementTopology::indexOf(ElementKind kind, std::uint32_t id) const {
    const auto find = [id](const auto& items) -> std::optional<std::uint32_t> {
        // Ids are usually index + 1; check that first, then scan.
        if (id >= 1 && id <= items.size() && items[id - 1].id == id) return id - 1;
        for (std::uint32_t i = 0; i < items.size(); ++i) {
            if (items[i].id == id) return i;
        }
        return std::nullopt;
    };
    switch (kind) {
    case ElementKind::Vertex: return find(vertices);
    case ElementKind::Edge: return find(edges);
    case ElementKind::Face: return find(faces);
    }
    return std::nullopt;
}

std::uint32_t ElementTopology::idAt(ElementKind kind, std::uint32_t index) const {
    switch (kind) {
    case ElementKind::Vertex: return vertices.at(index).id;
    case ElementKind::Edge: return edges.at(index).id;
    case ElementKind::Face: return faces.at(index).id;
    }
    return 0;
}

ElementTopology buildTopology(const std::vector<FacePolygon>& polygons, double tolerance) {
    ElementTopology topo;
    const bool sheet = polygons.size() == 1; // an un-extruded profile is one two-sided face

    // Merge coincident corners. Objects have few corners, so a linear scan per
    // corner is cheaper than a spatial index and exact for the tolerance.
    const auto vertexFor = [&](const Vec3& p) {
        for (std::uint32_t i = 0; i < topo.vertices.size(); ++i) {
            if (glm::length(topo.vertices[i].position - p) <= tolerance) return i;
        }
        topo.vertices.push_back({static_cast<std::uint32_t>(topo.vertices.size() + 1), p, {}});
        return static_cast<std::uint32_t>(topo.vertices.size() - 1);
    };
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> edgeIndex;
    const auto edgeFor = [&](std::uint32_t a, std::uint32_t b) {
        const auto key = std::minmax(a, b);
        if (const auto it = edgeIndex.find(key); it != edgeIndex.end()) return it->second;
        const auto e = static_cast<std::uint32_t>(topo.edges.size());
        topo.edges.push_back({e + 1, key.first, key.second, {}});
        topo.vertices[key.first].edges.push_back(e);
        topo.vertices[key.second].edges.push_back(e);
        edgeIndex.emplace(key, e);
        return e;
    };

    for (const auto& poly : polygons) {
        if (poly.loop.size() < 3) continue;
        const auto f = static_cast<std::uint32_t>(topo.faces.size());
        ElementTopology::Face face;
        face.id = f + 1;
        face.logical = poly.id;
        face.normal = poly.normal;
        face.twoSided = sheet;
        for (const auto& p : poly.loop) {
            const auto v = vertexFor(p);
            if (face.loop.empty() || face.loop.back() != v) face.loop.push_back(v);
        }
        if (face.loop.size() > 1 && face.loop.front() == face.loop.back()) face.loop.pop_back();
        if (face.loop.size() < 3) continue;
        for (std::size_t i = 0; i < face.loop.size(); ++i) {
            const auto e = edgeFor(face.loop[i], face.loop[(i + 1) % face.loop.size()]);
            face.edges.push_back(e);
            topo.edges[e].faces.push_back(f);
        }
        // Fan triangulation, as buildMesh does (the profiles are convex).
        for (std::uint32_t i = 1; i + 1 < face.loop.size(); ++i) {
            topo.triangles.push_back({f, {face.loop[0], face.loop[i], face.loop[i + 1]}});
        }
        topo.faces.push_back(std::move(face));
    }
    return topo;
}

ElementTopology buildTopology(const ProfileObject& object) { return buildTopology(buildFaces(object)); }

/// Profile edges are returned as indices (they are a drawing aid, not a selection).
std::vector<std::uint32_t> profileEdges(const ElementTopology& topo, const Vec3& eye) {
    std::vector<std::uint32_t> out;
    for (std::uint32_t e = 0; e < topo.edges.size(); ++e) {
        const auto& edge = topo.edges[e];
        if (edge.faces.size() != 2) {
            out.push_back(e); // border (or non-manifold) edge: always an outline
            continue;
        }
        const Vec3 at = topo.vertices[edge.a].position;
        const auto facing = [&](std::uint32_t f) { return glm::dot(topo.faces[f].normal, eye - at) > 0.0; };
        if (facing(edge.faces[0]) != facing(edge.faces[1])) out.push_back(e);
    }
    return out;
}

namespace {

void addUnique(std::vector<ElementRef>& out, const ElementRef& ref) {
    if (std::find(out.begin(), out.end(), ref) == out.end()) out.push_back(ref);
}

} // namespace

std::vector<ElementRef> expandBounded(ObjectId object, const ElementTopology& topo, ElementKind kind, std::uint32_t elementId) {
    std::vector<ElementRef> out;
    const auto index = topo.indexOf(kind, elementId);
    if (!index) return out;
    const std::uint32_t id = *index;
    switch (kind) {
    case ElementKind::Face:
        out.push_back({object, ElementKind::Face, elementId});
        for (const auto e : topo.faces[id].edges) addUnique(out, {object, ElementKind::Edge, topo.edges[e].id});
        break;
    case ElementKind::Edge:
        out.push_back({object, ElementKind::Edge, elementId});
        for (const auto f : topo.edges[id].faces) addUnique(out, {object, ElementKind::Face, topo.faces[f].id});
        break;
    case ElementKind::Vertex:
        for (const auto e : topo.vertices[id].edges) addUnique(out, {object, ElementKind::Edge, topo.edges[e].id});
        break;
    }
    return out;
}

std::vector<ElementRef> expandConnected(ObjectId object, const ElementTopology& topo, ElementKind kind, std::uint32_t elementId) {
    const auto index = topo.indexOf(kind, elementId);
    if (!index) return {};
    const std::uint32_t id = *index;
    // Flood over vertices: two elements are connected when they share a vertex.
    std::vector<bool> seenVertex(topo.vertices.size(), false);
    std::vector<std::uint32_t> stack;
    const auto seed = [&](std::uint32_t v) {
        if (v < seenVertex.size() && !seenVertex[v]) {
            seenVertex[v] = true;
            stack.push_back(v);
        }
    };
    switch (kind) {
    case ElementKind::Vertex: seed(id); break;
    case ElementKind::Edge:
        if (id < topo.edges.size()) {
            seed(topo.edges[id].a);
            seed(topo.edges[id].b);
        }
        break;
    case ElementKind::Face:
        if (id < topo.faces.size()) {
            for (const auto v : topo.faces[id].loop) seed(v);
        }
        break;
    }
    std::vector<bool> edgeIn(topo.edges.size(), false);
    while (!stack.empty()) {
        const auto v = stack.back();
        stack.pop_back();
        for (const auto e : topo.vertices[v].edges) {
            edgeIn[e] = true;
            seed(topo.edges[e].a);
            seed(topo.edges[e].b);
        }
    }
    std::vector<ElementRef> out;
    for (std::uint32_t e = 0; e < topo.edges.size(); ++e) {
        if (edgeIn[e]) out.push_back({object, ElementKind::Edge, topo.edges[e].id});
    }
    for (std::uint32_t f = 0; f < topo.faces.size(); ++f) {
        const auto& loop = topo.faces[f].loop;
        if (!loop.empty() && seenVertex[loop.front()]) out.push_back({object, ElementKind::Face, topo.faces[f].id});
    }
    return out;
}

} // namespace pf
