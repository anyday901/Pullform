#include "app/tools/orbit_tool.hpp"

namespace pf {

std::optional<std::string> OrbitTool::submit(std::string_view /*text*/) {
    return std::string("The Orbit tool takes no measurements.");
}

std::string OrbitTool::hint() const { return "Orbit: drag with the left button to orbit. Right-drag pans, the wheel zooms."; }

} // namespace pf
