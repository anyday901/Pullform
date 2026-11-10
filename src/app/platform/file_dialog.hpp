#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace pf {

/// Native open/save dialogs behind a small interface (nativefiledialog-extended:
/// Win32 IFileDialog, macOS NSOpenPanel, GTK on Linux). Calls block until the
/// user closes the dialog. An empty result means the user cancelled.
class FileDialogs {
public:
    FileDialogs();
    ~FileDialogs();
    FileDialogs(const FileDialogs&) = delete;
    FileDialogs& operator=(const FileDialogs&) = delete;

    [[nodiscard]] bool available() const { return available_; }
    [[nodiscard]] const std::string& error() const { return error_; }

    std::optional<std::filesystem::path> openProject();
    std::optional<std::filesystem::path> saveProject(const std::string& defaultName);

private:
    bool available_ = false;
    std::string error_;
};

} // namespace pf
