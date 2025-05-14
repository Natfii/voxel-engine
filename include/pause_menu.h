#pragma once
#include <GLFW/glfw3.h>

class PauseMenu {
public:
    PauseMenu(GLFWwindow* window);
    bool render();

private:
    GLFWwindow* window;
};
