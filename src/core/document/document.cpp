#include "core/document/document.hpp"

#include <charconv>

namespace pf {

std::string idString(ObjectId id) { return "obj-" + std::to_string(id.value); }

std::optional<ObjectId> parseObjectId(std::string_view text) {
    constexpr std::string_view prefix = "obj-";
    if (text.size() <= prefix.size() || text.substr(0, prefix.size()) != prefix) return std::nullopt;
    const auto digits = text.substr(prefix.size());
    if (digits.size() > 15 || digits.front() == '0') return std::nullopt; // canonical, below 2^53
    for (char c : digits) {
        if (c < '0' || c > '9') return std::nullopt;
    }
    std::uint64_t value = 0;
    const auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (ec != std::errc{} || ptr != digits.data() + digits.size() || value == 0) return std::nullopt;
    return ObjectId{value};
}

const ProfileObject* Document::find(ObjectId id) const {
    for (const auto& o : objects) {
        if (o && o->id == id) return o.get();
    }
    return nullptr;
}

std::ptrdiff_t Document::indexOf(ObjectId id) const {
    for (std::size_t i = 0; i < objects.size(); ++i) {
        if (objects[i] && objects[i]->id == id) return static_cast<std::ptrdiff_t>(i);
    }
    return -1;
}

bool sameContent(const Document& a, const Document& b) {
    if (a.displayUnit != b.displayUnit || a.nextId != b.nextId || a.objects.size() != b.objects.size()) return false;
    for (std::size_t i = 0; i < a.objects.size(); ++i) {
        if (!a.objects[i] || !b.objects[i] || !(*a.objects[i] == *b.objects[i])) return false;
    }
    return true;
}

std::string faceKey(FaceId face) {
    switch (face.kind) {
    case FaceKind::Profile: return "profile";
    case FaceKind::Base: return "base";
    case FaceKind::Cap: return "cap";
    case FaceKind::Side: return "side-" + std::to_string(face.side);
    }
    return "profile";
}

std::string faceLabel(const std::optional<FaceId>& face) {
    if (!face) return "Whole object";
    switch (face->kind) {
    case FaceKind::Profile: return "Face";
    case FaceKind::Base: return "Base";
    case FaceKind::Cap: return "Cap (top)";
    case FaceKind::Side: return "Side " + std::to_string(face->side + 1);
    }
    return "Face";
}

} // namespace pf
