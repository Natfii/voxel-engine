/**
 * @file key_bindings.cpp
 * @brief Implementation of configurable key bindings
 */

#include "key_bindings.h"
#include "config.h"
#include "logger.h"
#include <algorithm>
#include <cctype>

// Static member initialization
std::unordered_map<std::string, int> KeyBindings::s_keyNameToCode;
std::unordered_map<int, std::string> KeyBindings::s_codeToKeyName;
bool KeyBindings::s_mapInitialized = false;

KeyBindings& KeyBindings::instance() {
    static KeyBindings instance;
    return instance;
}

void KeyBindings::initKeyMap() {
    if (s_mapInitialized) return;

    // Letters A-Z
    for (char c = 'A'; c <= 'Z'; ++c) {
        std::string name(1, c);
        int code = GLFW_KEY_A + (c - 'A');
        s_keyNameToCode[name] = code;
        s_codeToKeyName[code] = name;
    }

    // Numbers 0-9
    for (char c = '0'; c <= '9'; ++c) {
        std::string name(1, c);
        int code = GLFW_KEY_0 + (c - '0');
        s_keyNameToCode[name] = code;
        s_codeToKeyName[code] = name;
    }

    // Function keys F1-F12
    for (int i = 1; i <= 12; ++i) {
        std::string name = "F" + std::to_string(i);
        int code = GLFW_KEY_F1 + (i - 1);
        s_keyNameToCode[name] = code;
        s_codeToKeyName[code] = name;
    }

    // Special keys
    s_keyNameToCode["SPACE"] = GLFW_KEY_SPACE;
    s_keyNameToCode["ENTER"] = GLFW_KEY_ENTER;
    s_keyNameToCode["RETURN"] = GLFW_KEY_ENTER;
    s_keyNameToCode["TAB"] = GLFW_KEY_TAB;
    s_keyNameToCode["BACKSPACE"] = GLFW_KEY_BACKSPACE;
    s_keyNameToCode["ESCAPE"] = GLFW_KEY_ESCAPE;
    s_keyNameToCode["ESC"] = GLFW_KEY_ESCAPE;

    // Arrow keys
    s_keyNameToCode["UP"] = GLFW_KEY_UP;
    s_keyNameToCode["DOWN"] = GLFW_KEY_DOWN;
    s_keyNameToCode["LEFT"] = GLFW_KEY_LEFT;
    s_keyNameToCode["RIGHT"] = GLFW_KEY_RIGHT;
    s_keyNameToCode["ARROW_UP"] = GLFW_KEY_UP;
    s_keyNameToCode["ARROW_DOWN"] = GLFW_KEY_DOWN;
    s_keyNameToCode["ARROW_LEFT"] = GLFW_KEY_LEFT;
    s_keyNameToCode["ARROW_RIGHT"] = GLFW_KEY_RIGHT;

    // Modifier keys
    s_keyNameToCode["LEFT_SHIFT"] = GLFW_KEY_LEFT_SHIFT;
    s_keyNameToCode["RIGHT_SHIFT"] = GLFW_KEY_RIGHT_SHIFT;
    s_keyNameToCode["SHIFT"] = GLFW_KEY_LEFT_SHIFT;
    s_keyNameToCode["LSHIFT"] = GLFW_KEY_LEFT_SHIFT;
    s_keyNameToCode["RSHIFT"] = GLFW_KEY_RIGHT_SHIFT;

    s_keyNameToCode["LEFT_CONTROL"] = GLFW_KEY_LEFT_CONTROL;
    s_keyNameToCode["RIGHT_CONTROL"] = GLFW_KEY_RIGHT_CONTROL;
    s_keyNameToCode["CONTROL"] = GLFW_KEY_LEFT_CONTROL;
    s_keyNameToCode["CTRL"] = GLFW_KEY_LEFT_CONTROL;
    s_keyNameToCode["LCTRL"] = GLFW_KEY_LEFT_CONTROL;
    s_keyNameToCode["RCTRL"] = GLFW_KEY_RIGHT_CONTROL;
    s_keyNameToCode["LEFT_CTRL"] = GLFW_KEY_LEFT_CONTROL;
    s_keyNameToCode["RIGHT_CTRL"] = GLFW_KEY_RIGHT_CONTROL;

    s_keyNameToCode["LEFT_ALT"] = GLFW_KEY_LEFT_ALT;
    s_keyNameToCode["RIGHT_ALT"] = GLFW_KEY_RIGHT_ALT;
    s_keyNameToCode["ALT"] = GLFW_KEY_LEFT_ALT;
    s_keyNameToCode["LALT"] = GLFW_KEY_LEFT_ALT;
    s_keyNameToCode["RALT"] = GLFW_KEY_RIGHT_ALT;

    // Other keys
    s_keyNameToCode["INSERT"] = GLFW_KEY_INSERT;
    s_keyNameToCode["DELETE"] = GLFW_KEY_DELETE;
    s_keyNameToCode["HOME"] = GLFW_KEY_HOME;
    s_keyNameToCode["END"] = GLFW_KEY_END;
    s_keyNameToCode["PAGE_UP"] = GLFW_KEY_PAGE_UP;
    s_keyNameToCode["PAGE_DOWN"] = GLFW_KEY_PAGE_DOWN;
    s_keyNameToCode["PAGEUP"] = GLFW_KEY_PAGE_UP;
    s_keyNameToCode["PAGEDOWN"] = GLFW_KEY_PAGE_DOWN;

    // Punctuation
    s_keyNameToCode["COMMA"] = GLFW_KEY_COMMA;
    s_keyNameToCode["PERIOD"] = GLFW_KEY_PERIOD;
    s_keyNameToCode["DOT"] = GLFW_KEY_PERIOD;
    s_keyNameToCode["SLASH"] = GLFW_KEY_SLASH;
    s_keyNameToCode["BACKSLASH"] = GLFW_KEY_BACKSLASH;
    s_keyNameToCode["SEMICOLON"] = GLFW_KEY_SEMICOLON;
    s_keyNameToCode["APOSTROPHE"] = GLFW_KEY_APOSTROPHE;
    s_keyNameToCode["QUOTE"] = GLFW_KEY_APOSTROPHE;
    s_keyNameToCode["MINUS"] = GLFW_KEY_MINUS;
    s_keyNameToCode["EQUAL"] = GLFW_KEY_EQUAL;
    s_keyNameToCode["EQUALS"] = GLFW_KEY_EQUAL;
    s_keyNameToCode["LEFT_BRACKET"] = GLFW_KEY_LEFT_BRACKET;
    s_keyNameToCode["RIGHT_BRACKET"] = GLFW_KEY_RIGHT_BRACKET;
    s_keyNameToCode["GRAVE"] = GLFW_KEY_GRAVE_ACCENT;
    s_keyNameToCode["TILDE"] = GLFW_KEY_GRAVE_ACCENT;
    s_keyNameToCode["BACKTICK"] = GLFW_KEY_GRAVE_ACCENT;

    // Numpad
    s_keyNameToCode["NUMPAD_0"] = GLFW_KEY_KP_0;
    s_keyNameToCode["NUMPAD_1"] = GLFW_KEY_KP_1;
    s_keyNameToCode["NUMPAD_2"] = GLFW_KEY_KP_2;
    s_keyNameToCode["NUMPAD_3"] = GLFW_KEY_KP_3;
    s_keyNameToCode["NUMPAD_4"] = GLFW_KEY_KP_4;
    s_keyNameToCode["NUMPAD_5"] = GLFW_KEY_KP_5;
    s_keyNameToCode["NUMPAD_6"] = GLFW_KEY_KP_6;
    s_keyNameToCode["NUMPAD_7"] = GLFW_KEY_KP_7;
    s_keyNameToCode["NUMPAD_8"] = GLFW_KEY_KP_8;
    s_keyNameToCode["NUMPAD_9"] = GLFW_KEY_KP_9;
    s_keyNameToCode["KP_0"] = GLFW_KEY_KP_0;
    s_keyNameToCode["KP_1"] = GLFW_KEY_KP_1;
    s_keyNameToCode["KP_2"] = GLFW_KEY_KP_2;
    s_keyNameToCode["KP_3"] = GLFW_KEY_KP_3;
    s_keyNameToCode["KP_4"] = GLFW_KEY_KP_4;
    s_keyNameToCode["KP_5"] = GLFW_KEY_KP_5;
    s_keyNameToCode["KP_6"] = GLFW_KEY_KP_6;
    s_keyNameToCode["KP_7"] = GLFW_KEY_KP_7;
    s_keyNameToCode["KP_8"] = GLFW_KEY_KP_8;
    s_keyNameToCode["KP_9"] = GLFW_KEY_KP_9;

    // Build reverse map for common keys
    s_codeToKeyName[GLFW_KEY_SPACE] = "SPACE";
    s_codeToKeyName[GLFW_KEY_ENTER] = "ENTER";
    s_codeToKeyName[GLFW_KEY_TAB] = "TAB";
    s_codeToKeyName[GLFW_KEY_ESCAPE] = "ESCAPE";
    s_codeToKeyName[GLFW_KEY_LEFT_SHIFT] = "LEFT_SHIFT";
    s_codeToKeyName[GLFW_KEY_RIGHT_SHIFT] = "RIGHT_SHIFT";
    s_codeToKeyName[GLFW_KEY_LEFT_CONTROL] = "LEFT_CONTROL";
    s_codeToKeyName[GLFW_KEY_RIGHT_CONTROL] = "RIGHT_CONTROL";
    s_codeToKeyName[GLFW_KEY_LEFT_ALT] = "LEFT_ALT";
    s_codeToKeyName[GLFW_KEY_RIGHT_ALT] = "RIGHT_ALT";

    s_mapInitialized = true;
}

int KeyBindings::keyNameToGLFW(const std::string& keyName) {
    initKeyMap();

    // Convert to uppercase for lookup
    std::string upper = keyName;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return std::toupper(c); });

    auto it = s_keyNameToCode.find(upper);
    if (it != s_keyNameToCode.end()) {
        return it->second;
    }

    return GLFW_KEY_UNKNOWN;
}

std::string KeyBindings::glfwToKeyName(int keyCode) {
    initKeyMap();

    auto it = s_codeToKeyName.find(keyCode);
    if (it != s_codeToKeyName.end()) {
        return it->second;
    }

    return "UNKNOWN";
}

void KeyBindings::loadFromConfig() {
    initKeyMap();

    Config& config = Config::instance();

    // Helper lambda to load a key binding
    auto loadKey = [&](const std::string& configKey, int& target, int defaultKey) {
        std::string keyName = config.getString("Controls", configKey, "");
        if (!keyName.empty()) {
            int code = keyNameToGLFW(keyName);
            if (code != GLFW_KEY_UNKNOWN) {
                target = code;
                Logger::info() << "Key binding: " << configKey << " = " << keyName;
            } else {
                Logger::warning() << "Unknown key name for " << configKey << ": " << keyName;
                target = defaultKey;
            }
        } else {
            target = defaultKey;
        }
    };

    // Load movement keys
    loadKey("key_forward", moveForward, GLFW_KEY_W);
    loadKey("key_backward", moveBackward, GLFW_KEY_S);
    loadKey("key_left", moveLeft, GLFW_KEY_A);
    loadKey("key_right", moveRight, GLFW_KEY_D);
    loadKey("key_jump", jump, GLFW_KEY_SPACE);
    loadKey("key_sprint", sprint, GLFW_KEY_LEFT_SHIFT);
    loadKey("key_crouch", crouch, GLFW_KEY_LEFT_CONTROL);

    // Load action keys
    loadKey("key_noclip", noclip, GLFW_KEY_N);
    loadKey("key_third_person", thirdPerson, GLFW_KEY_F3);

    // Load UI keys
    loadKey("key_console", toggleConsole, GLFW_KEY_F9);
    loadKey("key_inventory", toggleInventory, GLFW_KEY_I);
    loadKey("key_pause", pause, GLFW_KEY_ESCAPE);

    // Load other settings
    mouseSensitivity = config.getFloat("Controls", "mouse_sensitivity", 0.1f);
    sprintMultiplier = config.getFloat("Controls", "sprint_multiplier", 1.5f);

    std::string sprintToggleStr = config.getString("Controls", "sprint_toggle", "false");
    sprintToggle = (sprintToggleStr == "true" || sprintToggleStr == "1" || sprintToggleStr == "yes");

    Logger::info() << "Key bindings loaded from config";
}
