#pragma once

#include "core/commands/operations.hpp"

// Building blocks shared by the operation files (operations.cpp, cleanup.cpp).

namespace pf {

/// Puts `changed` in place of the object with the same id, after validating it.
Expected<OpResult> replaceObject(const Document& doc, ProfileObject changed);

/// The object as a mesh group: a profile group becomes one (same ids as
/// meshFromProfile, so corner anchors stay valid); a mesh group is unchanged.
ProfileObject asMeshGroup(const ProfileObject& o);

} // namespace pf
