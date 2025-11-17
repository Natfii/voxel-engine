// Load dialog implementation for MainMenu
#include "main_menu.h"
#include "imgui.h"
#include <filesystem>
#include <fstream>

std::vector<std::string> MainMenu::scanAvailableWorlds() {
    std::vector<std::string> worlds;
    namespace fs = std::filesystem;

    try {
        std::string worldsDir = "worlds";
        if (!fs::exists(worldsDir)) {
            return worlds;  // Empty list if no worlds directory
        }

        // Scan worlds directory for subdirectories with world.meta file
        for (const auto& entry : fs::directory_iterator(worldsDir)) {
            if (entry.is_directory()) {
                fs::path metaPath = entry.path() / "world.meta";
                if (fs::exists(metaPath)) {
                    worlds.push_back(entry.path().string());
                }
            }
        }
    } catch (const std::exception& e) {
        // If scan fails, just return empty list
    }

    return worlds;
}

void MainMenu::renderLoadWorldDialog() {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 displaySize = io.DisplaySize;

    float centerX = displaySize.x * 0.5f;
    float centerY = displaySize.y * 0.5f;
    float dialogWidth = 500.0f;
    float dialogHeight = 400.0f;
    float buttonWidth = 150.0f;
    float buttonHeight = 35.0f;

    // Semi-transparent background for dialog
    ImGui::SetCursorPos(ImVec2(centerX - dialogWidth * 0.5f, centerY - dialogHeight * 0.5f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.2f, 0.2f, 0.25f, 0.95f));
    ImGui::BeginChild("LoadDialog", ImVec2(dialogWidth, dialogHeight), true);

    // Dialog title
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::SetWindowFontScale(1.5f);
    ImGui::SetCursorPosX((dialogWidth - ImGui::CalcTextSize("Load World").x * 1.5f) * 0.5f);
    ImGui::Text("Load World");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // World list
    if (availableWorlds.empty()) {
        ImGui::Text("No saved worlds found.");
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Play a new game and it will be saved automatically.");
    } else {
        ImGui::Text("Select a world to load:");
        ImGui::Spacing();

        // Scrollable list of worlds
        ImGui::BeginChild("WorldList", ImVec2(dialogWidth - 40.0f, dialogHeight - 150.0f), true);

        for (size_t i = 0; i < availableWorlds.size(); i++) {
            namespace fs = std::filesystem;
            std::string worldName = fs::path(availableWorlds[i]).filename().string();

            // Highlight selected world
            bool isSelected = (selectedWorldIndex == (int)i);
            if (isSelected) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
            }

            if (ImGui::Button(worldName.c_str(), ImVec2(dialogWidth - 60.0f, 35.0f))) {
                selectedWorldIndex = (int)i;
            }

            if (isSelected) {
                ImGui::PopStyleColor();
            }

            ImGui::Spacing();
        }

        ImGui::EndChild();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Load and Back buttons
    float totalButtonWidth = buttonWidth * 2 + 20.0f;
    float startX = (dialogWidth - totalButtonWidth) * 0.5f;

    ImGui::SetCursorPosX(startX);
    ImGui::SetCursorPosY(dialogHeight - buttonHeight - 20.0f);

    // Load button (disabled if no world selected)
    if (selectedWorldIndex < 0) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
        ImGui::Button("Load World", ImVec2(buttonWidth, buttonHeight));
        ImGui::PopStyleVar();
    } else {
        if (ImGui::Button("Load World", ImVec2(buttonWidth, buttonHeight))) {
            showLoadDialog = false;  // This will trigger LOAD_GAME action in render()
        }
    }

    ImGui::SameLine(0, 20.0f);
    if (ImGui::Button("Back", ImVec2(buttonWidth, buttonHeight))) {
        showLoadDialog = false;
        selectedWorldIndex = -1;
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}
