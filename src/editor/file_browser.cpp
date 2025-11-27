/**
 * @file file_browser.cpp
 * @brief Simple ImGui file browser implementation
 */

#include "editor/file_browser.h"
#include <algorithm>

namespace fs = std::filesystem;

FileBrowser::FileBrowser() {
    // Start in assets directory if it exists
    if (fs::exists("assets")) {
        m_currentPath = fs::absolute("assets").string();
    } else {
        m_currentPath = fs::current_path().string();
    }
}

void FileBrowser::open(Mode mode, const std::string& title,
                       const std::vector<std::string>& filters,
                       const std::string& startPath) {
    m_isOpen = true;
    m_mode = mode;
    m_title = title;
    m_filters = filters;
    m_selectedIndex = -1;
    m_selectedPath.clear();
    m_filenameBuffer[0] = '\0';

    if (!startPath.empty() && fs::exists(startPath)) {
        if (fs::is_directory(startPath)) {
            m_currentPath = fs::absolute(startPath).string();
        } else {
            m_currentPath = fs::absolute(startPath).parent_path().string();
            // Pre-fill filename for save mode
            std::string fname = fs::path(startPath).filename().string();
            strncpy(m_filenameBuffer, fname.c_str(), sizeof(m_filenameBuffer) - 1);
        }
    }

    refreshDirectory();
}

void FileBrowser::close() {
    m_isOpen = false;
}

void FileBrowser::refreshDirectory() {
    m_entries.clear();
    m_selectedIndex = -1;

    try {
        // Add parent directory entry
        fs::path current(m_currentPath);
        if (current.has_parent_path() && current.parent_path() != current) {
            m_entries.push_back({"..", true, 0});
        }

        // List directory contents
        for (const auto& entry : fs::directory_iterator(m_currentPath)) {
            FileEntry fe;
            fe.name = entry.path().filename().string();
            fe.isDirectory = entry.is_directory();
            fe.size = fe.isDirectory ? 0 : entry.file_size();

            // Skip hidden files (starting with .)
            if (!fe.name.empty() && fe.name[0] == '.' && fe.name != "..") {
                continue;
            }

            // Apply filter for files
            if (!fe.isDirectory && !m_filters.empty()) {
                if (!matchesFilter(fe.name)) {
                    continue;
                }
            }

            m_entries.push_back(fe);
        }

        // Sort: directories first, then alphabetically
        std::sort(m_entries.begin(), m_entries.end(), [](const FileEntry& a, const FileEntry& b) {
            if (a.name == "..") return true;
            if (b.name == "..") return false;
            if (a.isDirectory != b.isDirectory) return a.isDirectory;
            return a.name < b.name;
        });

    } catch (const std::exception&) {
        // Directory access error - go to parent
        fs::path current(m_currentPath);
        if (current.has_parent_path()) {
            m_currentPath = current.parent_path().string();
            refreshDirectory();
        }
    }
}

bool FileBrowser::matchesFilter(const std::string& filename) const {
    if (m_filters.empty()) return true;

    for (const auto& filter : m_filters) {
        if (filename.size() >= filter.size()) {
            std::string ext = filename.substr(filename.size() - filter.size());
            // Case-insensitive compare
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            std::string filterLower = filter;
            std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);
            if (ext == filterLower) {
                return true;
            }
        }
    }
    return false;
}

bool FileBrowser::render() {
    if (!m_isOpen) return false;

    bool fileSelected = false;
    ImGui::SetNextWindowSize(ImVec2(600, 450), ImGuiCond_FirstUseEver);

    std::string windowTitle = m_title + "###FileBrowser";
    if (ImGui::Begin(windowTitle.c_str(), &m_isOpen, ImGuiWindowFlags_NoCollapse)) {
        // Current path display
        ImGui::Text("Path: %s", m_currentPath.c_str());
        ImGui::Separator();

        // File list
        ImGui::BeginChild("FileList", ImVec2(0, -80), true);
        for (size_t i = 0; i < m_entries.size(); ++i) {
            const auto& entry = m_entries[i];

            // Icon prefix
            const char* icon = entry.isDirectory ? "[D] " : "    ";
            std::string label = icon + entry.name;

            bool isSelected = (m_selectedIndex == static_cast<int>(i));
            if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick)) {
                m_selectedIndex = static_cast<int>(i);

                if (ImGui::IsMouseDoubleClicked(0)) {
                    if (entry.isDirectory) {
                        // Navigate into directory
                        if (entry.name == "..") {
                            fs::path current(m_currentPath);
                            m_currentPath = current.parent_path().string();
                        } else {
                            m_currentPath = (fs::path(m_currentPath) / entry.name).string();
                        }
                        refreshDirectory();
                    } else {
                        // Double-click on file = select it
                        m_selectedPath = (fs::path(m_currentPath) / entry.name).string();
                        m_filename = entry.name;
                        fileSelected = true;
                        m_isOpen = false;
                    }
                } else if (!entry.isDirectory) {
                    // Single click on file - update filename buffer
                    strncpy(m_filenameBuffer, entry.name.c_str(), sizeof(m_filenameBuffer) - 1);
                }
            }
        }
        ImGui::EndChild();

        // Filename input (for save mode)
        ImGui::Separator();
        if (m_mode == Mode::SAVE) {
            ImGui::Text("Filename:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-150);
            ImGui::InputText("##filename", m_filenameBuffer, sizeof(m_filenameBuffer));
        } else {
            // Show selected file for open mode
            if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_entries.size()) &&
                !m_entries[m_selectedIndex].isDirectory) {
                ImGui::Text("Selected: %s", m_entries[m_selectedIndex].name.c_str());
            } else {
                ImGui::Text("Selected: (none)");
            }
        }

        // Filter info
        if (!m_filters.empty()) {
            ImGui::SameLine();
            std::string filterStr = "Filters: ";
            for (size_t i = 0; i < m_filters.size(); ++i) {
                if (i > 0) filterStr += ", ";
                filterStr += "*" + m_filters[i];
            }
            ImGui::TextDisabled("%s", filterStr.c_str());
        }

        // Buttons
        ImGui::Separator();
        float buttonWidth = 100;
        float spacing = ImGui::GetStyle().ItemSpacing.x;
        float totalWidth = buttonWidth * 2 + spacing;
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - totalWidth) * 0.5f);

        const char* confirmText = (m_mode == Mode::SAVE) ? "Save" : "Open";
        if (ImGui::Button(confirmText, ImVec2(buttonWidth, 0))) {
            if (m_mode == Mode::SAVE) {
                // Save mode - use filename buffer
                if (strlen(m_filenameBuffer) > 0) {
                    m_selectedPath = (fs::path(m_currentPath) / m_filenameBuffer).string();
                    m_filename = m_filenameBuffer;
                    fileSelected = true;
                    m_isOpen = false;
                }
            } else {
                // Open mode - use selected file
                if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_entries.size()) &&
                    !m_entries[m_selectedIndex].isDirectory) {
                    m_selectedPath = (fs::path(m_currentPath) / m_entries[m_selectedIndex].name).string();
                    m_filename = m_entries[m_selectedIndex].name;
                    fileSelected = true;
                    m_isOpen = false;
                }
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0))) {
            m_isOpen = false;
        }
    }
    ImGui::End();

    return fileSelected;
}
