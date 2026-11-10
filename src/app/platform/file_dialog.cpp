#include "app/platform/file_dialog.hpp"

#include "core/document/project_file.hpp"

#include <nfd.h>

namespace pf {

namespace {

constexpr nfdu8filteritem_t kFilters[] = {{"Pullform project", "json"}};

std::filesystem::path fromUtf8(const char* s) {
    const std::string u8(s);
    return std::filesystem::path(std::u8string(u8.begin(), u8.end()));
}

} // namespace

FileDialogs::FileDialogs() {
    available_ = NFD_Init() == NFD_OKAY;
    if (!available_) error_ = NFD_GetError() ? NFD_GetError() : "File dialogs are unavailable.";
}

FileDialogs::~FileDialogs() {
    if (available_) NFD_Quit();
}

std::optional<std::filesystem::path> FileDialogs::openProject() {
    if (!available_) return std::nullopt;
    nfdu8char_t* out = nullptr;
    const nfdresult_t r = NFD_OpenDialogU8(&out, kFilters, 1, nullptr);
    if (r != NFD_OKAY) {
        error_ = r == NFD_ERROR && NFD_GetError() ? NFD_GetError() : "";
        return std::nullopt;
    }
    auto path = fromUtf8(out);
    NFD_FreePathU8(out);
    return path;
}

std::optional<std::filesystem::path> FileDialogs::saveProject(const std::string& defaultName) {
    if (!available_) return std::nullopt;
    nfdu8char_t* out = nullptr;
    const nfdresult_t r = NFD_SaveDialogU8(&out, kFilters, 1, nullptr, defaultName.c_str());
    if (r != NFD_OKAY) {
        error_ = r == NFD_ERROR && NFD_GetError() ? NFD_GetError() : "";
        return std::nullopt;
    }
    auto path = withProjectExtension(fromUtf8(out));
    NFD_FreePathU8(out);
    return path;
}

} // namespace pf
