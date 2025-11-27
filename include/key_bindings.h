/**
 * @file key_bindings.h
 * @brief Configurable key bindings system
 *
 * Loads key bindings from config.ini and provides GLFW key codes
 * for all game controls. Supports remapping via config file.
 */

#pragma once

#include <GLFW/glfw3.h>
#include <string>
#include <unordered_map>

/**
 * @brief Manages configurable key bindings loaded from config.ini
 *
 * Singleton class that provides GLFW key codes for all game controls.
 * Keys are loaded from the [Controls] section of config.ini.
 */
class KeyBindings {
public:
    /**
     * @brief Get singleton instance
     */
    static KeyBindings& instance();

    /**
     * @brief Load key bindings from config
     * Call this after Config::loadFromFile()
     */
    void loadFromConfig();

    /**
     * @brief Convert key name string to GLFW key code
     * @param keyName Key name (e.g., "W", "SPACE", "LEFT_SHIFT")
     * @return GLFW key code, or GLFW_KEY_UNKNOWN if not found
     */
    static int keyNameToGLFW(const std::string& keyName);

    /**
     * @brief Convert GLFW key code to key name string
     * @param keyCode GLFW key code
     * @return Key name string
     */
    static std::string glfwToKeyName(int keyCode);

    // ========== Movement Keys ==========
    int moveForward = GLFW_KEY_W;
    int moveBackward = GLFW_KEY_S;
    int moveLeft = GLFW_KEY_A;
    int moveRight = GLFW_KEY_D;
    int jump = GLFW_KEY_SPACE;
    int sprint = GLFW_KEY_LEFT_SHIFT;
    int crouch = GLFW_KEY_LEFT_CONTROL;

    // ========== Action Keys ==========
    int noclip = GLFW_KEY_N;
    int thirdPerson = GLFW_KEY_F3;

    // ========== UI Keys ==========
    int toggleConsole = GLFW_KEY_F9;
    int toggleInventory = GLFW_KEY_I;
    int pause = GLFW_KEY_ESCAPE;
    int cursorUnlock = GLFW_KEY_RIGHT_ALT;  // Temporarily unlock cursor

    // ========== Mouse Sensitivity ==========
    float mouseSensitivity = 0.1f;
    float sprintMultiplier = 1.5f;
    bool sprintToggle = false;

private:
    KeyBindings() = default;

    /**
     * @brief Initialize the key name to GLFW code mapping
     */
    static void initKeyMap();

    static std::unordered_map<std::string, int> s_keyNameToCode;
    static std::unordered_map<int, std::string> s_codeToKeyName;
    static bool s_mapInitialized;
};
