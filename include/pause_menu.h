#pragma once
#include <GLFW/glfw3.h>

enum class PauseMenuAction {
    NONE,           // Still paused
    RESUME,         // Resume game
    EXIT_TO_MENU,   // Exit to main menu (with save)
    QUIT            // Quit application
};

class PauseMenu {
public:
    PauseMenu(GLFWwindow* window);
    PauseMenuAction render();

private:
    GLFWwindow* window;
};
