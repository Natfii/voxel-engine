/**
 * @file file_browser.h
 * @brief Simple ImGui file browser dialog
 */

#pragma once

#include <imgui.h>
#include <string>
#include <vector>
#include <filesystem>

/**
 * @brief Simple file browser dialog for ImGui
 */
class FileBrowser {
public:
    enum class Mode {
        OPEN,
        SAVE
    };

    FileBrowser();

    /**
     * @brief Open the file browser
     * @param mode OPEN or SAVE mode
     * @param title Dialog title
     * @param filters File extension filters (e.g., {".rig", ".yaml"})
     * @param startPath Starting directory path
     */
    void open(Mode mode, const std::string& title,
              const std::vector<std::string>& filters = {},
              const std::string& startPath = "");

    /**
     * @brief Close the file browser
     */
    void close();

    /**
     * @brief Check if browser is open
     */
    bool isOpen() const { return m_isOpen; }

    /**
     * @brief Render the file browser dialog
     * @return true if a file was selected
     */
    bool render();

    /**
     * @brief Get the selected file path
     */
    const std::string& getSelectedPath() const { return m_selectedPath; }

    /**
     * @brief Get the filename for save dialogs
     */
    const std::string& getFilename() const { return m_filename; }

private:
    void refreshDirectory();
    bool matchesFilter(const std::string& filename) const;

    bool m_isOpen = false;
    Mode m_mode = Mode::OPEN;
    std::string m_title;
    std::string m_currentPath;
    std::string m_selectedPath;
    std::string m_filename;
    char m_filenameBuffer[256] = "";
    std::vector<std::string> m_filters;

    struct FileEntry {
        std::string name;
        bool isDirectory;
        size_t size;
    };
    std::vector<FileEntry> m_entries;
    int m_selectedIndex = -1;
};
