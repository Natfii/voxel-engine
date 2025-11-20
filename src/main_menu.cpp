#include "main_menu.h"
#include "imgui.h"
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <filesystem>
#include <iostream>

MainMenu::MainMenu(GLFWwindow* window) : window(window) {
    // Initialize random seed generator
    std::srand(std::time(nullptr));
}

MenuResult MainMenu::render() {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 displaySize = io.DisplaySize;

    // Full-screen overlay with semi-transparent background
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(displaySize, ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.15f, 0.95f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
    ImGui::Begin("MainMenu", nullptr, flags);

    MenuResult result;

    if (showSeedDialog) {
        renderSeedDialog();

        // Check if we should start the game
        if (!showSeedDialog && !showLoadDialog) {
            // Dialog was closed with "Start Game" button
            // Parse the seed from input buffer
            int seed = std::atoi(seedInputBuffer);
            if (seed == 0 && seedInputBuffer[0] != '0') {
                // Invalid input, use default
                seed = 1124345;
            }
            result.action = MenuAction::NEW_GAME;
            result.seed = seed;
            result.spawnRadius = spawnRadiusSlider;
            result.temperatureBias = temperatureSlider;
            result.moistureBias = moistureSlider;
            result.ageBias = ageSlider;
        }
    } else if (showLoadDialog) {
        renderLoadWorldDialog();

        // Check if a world was selected
        if (!showLoadDialog && selectedWorldIndex >= 0) {
            result.action = MenuAction::LOAD_GAME;
            result.worldPath = availableWorlds[selectedWorldIndex];
        }
    } else {
        renderMainButtons();
    }

    ImGui::End();
    ImGui::PopStyleColor();

    return result;
}

void MainMenu::renderMainButtons() {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 displaySize = io.DisplaySize;

    float centerX = displaySize.x * 0.5f;
    float centerY = displaySize.y * 0.5f;
    float buttonWidth = 200.0f;
    float buttonHeight = 40.0f;
    float buttonSpacing = 15.0f;

    // Title
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::SetCursorPos(ImVec2(centerX - 160.0f, centerY - 180.0f));
    ImGui::SetWindowFontScale(2.0f);
    ImGui::Text("NAVI Voxel Engine: Map Maker");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();

    // New Game button
    ImGui::SetCursorPos(ImVec2(centerX - buttonWidth * 0.5f, centerY - 60.0f));
    if (ImGui::Button("New Game", ImVec2(buttonWidth, buttonHeight))) {
        showSeedDialog = true;
    }

    // Load Game button
    ImGui::SetCursorPos(ImVec2(centerX - buttonWidth * 0.5f, centerY - 60.0f + (buttonHeight + buttonSpacing)));
    if (ImGui::Button("Load Game", ImVec2(buttonWidth, buttonHeight))) {
        showLoadDialog = true;
        selectedWorldIndex = -1;
        availableWorlds = scanAvailableWorlds();
    }

    // Host button (placeholder)
    ImGui::SetCursorPos(ImVec2(centerX - buttonWidth * 0.5f, centerY - 60.0f + 2 * (buttonHeight + buttonSpacing)));
    if (ImGui::Button("Host", ImVec2(buttonWidth, buttonHeight))) {
        showSeedDialog = false;
    }

    // Join button (placeholder)
    ImGui::SetCursorPos(ImVec2(centerX - buttonWidth * 0.5f, centerY - 60.0f + 3 * (buttonHeight + buttonSpacing)));
    if (ImGui::Button("Join", ImVec2(buttonWidth, buttonHeight))) {
        showSeedDialog = false;
    }

    // Quit button
    ImGui::SetCursorPos(ImVec2(centerX - buttonWidth * 0.5f, centerY - 60.0f + 4 * (buttonHeight + buttonSpacing)));
    if (ImGui::Button("Quit", ImVec2(buttonWidth, buttonHeight))) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }

    // Show "Coming soon" messages for placeholder buttons
    static float messageTimer = 0.0f;
    static bool showMessage = false;
    static std::string message = "";


    // Check if Host was clicked
    ImVec2 hostButtonPos(centerX - buttonWidth * 0.5f, centerY - 60.0f + 2 * (buttonHeight + buttonSpacing));
    ImVec2 hostButtonMax(hostButtonPos.x + buttonWidth, hostButtonPos.y + buttonHeight);
    if (ImGui::IsMouseClicked(0) &&
        io.MousePos.x >= hostButtonPos.x && io.MousePos.x <= hostButtonMax.x &&
        io.MousePos.y >= hostButtonPos.y && io.MousePos.y <= hostButtonMax.y) {
        message = "Multiplayer coming soon!";
        showMessage = true;
        messageTimer = 3.0f;
    }

    // Check if Join was clicked
    ImVec2 joinButtonPos(centerX - buttonWidth * 0.5f, centerY - 60.0f + 3 * (buttonHeight + buttonSpacing));
    ImVec2 joinButtonMax(joinButtonPos.x + buttonWidth, joinButtonPos.y + buttonHeight);
    if (ImGui::IsMouseClicked(0) &&
        io.MousePos.x >= joinButtonPos.x && io.MousePos.x <= joinButtonMax.x &&
        io.MousePos.y >= joinButtonPos.y && io.MousePos.y <= joinButtonMax.y) {
        message = "Multiplayer coming soon!";
        showMessage = true;
        messageTimer = 3.0f;
    }

    // Display message if active
    if (showMessage) {
        messageTimer -= ImGui::GetIO().DeltaTime;
        if (messageTimer <= 0.0f) {
            showMessage = false;
        } else {
            ImGui::SetCursorPos(ImVec2(centerX - 150.0f, centerY + 150.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
            ImGui::Text("%s", message.c_str());
            ImGui::PopStyleColor();
        }
    }
}

void MainMenu::renderSeedDialog() {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 displaySize = io.DisplaySize;

    float centerX = displaySize.x * 0.5f;
    float centerY = displaySize.y * 0.5f;
    float dialogWidth = 450.0f;
    float dialogHeight = 520.0f;
    float buttonWidth = 150.0f;
    float buttonHeight = 35.0f;

    // Semi-transparent background for dialog
    ImGui::SetCursorPos(ImVec2(centerX - dialogWidth * 0.5f, centerY - dialogHeight * 0.5f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.2f, 0.2f, 0.25f, 0.95f));
    ImGui::BeginChild("SeedDialog", ImVec2(dialogWidth, dialogHeight), true);

    // Dialog title
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::SetWindowFontScale(1.5f);
    ImGui::SetCursorPosX((dialogWidth - ImGui::CalcTextSize("New Game").x * 1.5f) * 0.5f);
    ImGui::Text("New Game");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Spacing();

    // Seed input label
    ImGui::Text("World Seed:");
    ImGui::Spacing();

    // Seed input field
    ImGui::SetNextItemWidth(dialogWidth - 40.0f);
    ImGui::InputText("##seed", seedInputBuffer, sizeof(seedInputBuffer), ImGuiInputTextFlags_CharsDecimal);

    ImGui::Spacing();
    ImGui::Spacing();

    // Random seed button
    ImGui::SetCursorPosX((dialogWidth - buttonWidth) * 0.5f);
    if (ImGui::Button("Random Seed", ImVec2(buttonWidth, buttonHeight))) {
        // Generate random seed (using std::rand)
        int randomSeed = std::rand() % 1000000;
        snprintf(seedInputBuffer, sizeof(seedInputBuffer), "%d", randomSeed);
    }

    ImGui::Spacing();
    ImGui::Spacing();

    // Spawn radius slider
    ImGui::Text("Initial Spawn Area:");
    ImGui::SetNextItemWidth(dialogWidth - 40.0f);
    ImGui::SliderInt("##spawnradius", &spawnRadiusSlider, 2, 8, "%d chunks radius");
    ImGui::TextDisabled("Larger areas take longer to generate");

    ImGui::Spacing();
    ImGui::Spacing();

    // Temperature slider
    ImGui::Text("Temperature:");
    ImGui::SetNextItemWidth(dialogWidth - 40.0f);
    ImGui::SliderFloat("##temperature", &temperatureSlider, -1.0f, 1.0f, "%.2f");
    ImGui::TextDisabled("Negative = colder world, Positive = hotter world");

    ImGui::Spacing();

    // Moisture slider
    ImGui::Text("Moisture:");
    ImGui::SetNextItemWidth(dialogWidth - 40.0f);
    ImGui::SliderFloat("##moisture", &moistureSlider, -1.0f, 1.0f, "%.2f");
    ImGui::TextDisabled("Negative = drier world, Positive = wetter world");

    ImGui::Spacing();

    // Age/Roughness slider
    ImGui::Text("Terrain Roughness:");
    ImGui::SetNextItemWidth(dialogWidth - 40.0f);
    ImGui::SliderFloat("##age", &ageSlider, -1.0f, 1.0f, "%.2f");
    ImGui::TextDisabled("Negative = flatter/smoother, Positive = mountainous");

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Start Game and Back buttons
    float totalButtonWidth = buttonWidth * 2 + 20.0f;  // 2 buttons + spacing
    float startX = (dialogWidth - totalButtonWidth) * 0.5f;

    ImGui::SetCursorPosX(startX);
    ImGui::SetCursorPosY(dialogHeight - buttonHeight - 20.0f);
    if (ImGui::Button("Start Game", ImVec2(buttonWidth, buttonHeight))) {
        showSeedDialog = false;  // This will trigger NEW_GAME action in render()
    }

    ImGui::SameLine(0, 20.0f);
    if (ImGui::Button("Back", ImVec2(buttonWidth, buttonHeight))) {
        showSeedDialog = false;
        // Reset to main menu without starting game
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}
