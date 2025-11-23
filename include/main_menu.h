#pragma once
#include <GLFW/glfw3.h>
#include <string>
#include <vector>

// Result of main menu interaction
enum class MenuAction {
    NONE,           // Still in menu
    NEW_GAME,       // Start new game with seed
    LOAD_GAME,      // Load saved game
    QUIT            // Exit application
};

struct MenuResult {
    MenuAction action = MenuAction::NONE;
    int seed = 0;              // World seed (if NEW_GAME)
    std::string worldPath = "";  // World path (if LOAD_GAME)
    int spawnRadius = 2;       // Initial spawn area radius in chunks (if NEW_GAME) - GPU warm-up ensures 60 FPS
    float temperatureBias = 0.0f;  // Temperature modifier (-1.0 to +1.0)
    float moistureBias = 0.0f;     // Moisture modifier (-1.0 to +1.0)
    float ageBias = 0.0f;          // Age/roughness modifier (-1.0 to +1.0)

    MenuResult() = default;
    MenuResult(MenuAction a, int s = 0) : action(a), seed(s) {}
    MenuResult(MenuAction a, const std::string& path) : action(a), worldPath(path) {}
};

class MainMenu {
public:
    MainMenu(GLFWwindow* window);

    // Render the menu and return the result
    // Returns MenuResult indicating what action was selected
    MenuResult render();

private:
    void renderMainButtons();
    void renderSeedDialog();
    void renderLoadWorldDialog();
    std::vector<std::string> scanAvailableWorlds();

    GLFWwindow* window;

    // Menu state
    bool showSeedDialog = false;
    bool showLoadDialog = false;
    bool startGameRequested = false;  // Track if "Start Game" was clicked (vs "Back")
    char seedInputBuffer[32] = "1124345";  // Default seed
    bool randomSeedRequested = false;
    int spawnRadiusSlider = 2;  // Initial spawn radius (2-8 chunks) - GPU warm-up ensures 60 FPS
    float temperatureSlider = 0.0f;  // Temperature bias (-1.0 to +1.0)
    float moistureSlider = 0.0f;     // Moisture bias (-1.0 to +1.0)
    float ageSlider = 0.0f;          // Age/roughness bias (-1.0 to +1.0)

    // Load dialog state
    std::vector<std::string> availableWorlds;
    int selectedWorldIndex = -1;
};
