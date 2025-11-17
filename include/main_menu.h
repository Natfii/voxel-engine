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
    int spawnRadius = 4;       // Initial spawn area radius in chunks (if NEW_GAME)

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
    char seedInputBuffer[32] = "1124345";  // Default seed
    bool randomSeedRequested = false;
    int spawnRadiusSlider = 4;  // Initial spawn radius (2-8 chunks)

    // Load dialog state
    std::vector<std::string> availableWorlds;
    int selectedWorldIndex = -1;
};
