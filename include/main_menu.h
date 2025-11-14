#pragma once
#include <GLFW/glfw3.h>
#include <string>

// Result of main menu interaction
enum class MenuAction {
    NONE,           // Still in menu
    NEW_GAME,       // Start new game with seed
    LOAD_GAME,      // Load saved game
    QUIT            // Exit application
};

struct MenuResult {
    MenuAction action = MenuAction::NONE;
    int seed = 0;   // World seed (if NEW_GAME)

    MenuResult() = default;
    MenuResult(MenuAction a, int s = 0) : action(a), seed(s) {}
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

    GLFWwindow* window;

    // Menu state
    bool showSeedDialog = false;
    char seedInputBuffer[32] = "1124345";  // Default seed
    bool randomSeedRequested = false;
};
