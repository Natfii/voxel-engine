#pragma once

// Centralized input state management
// Handles different input contexts (gameplay, menu, console, etc.)
class InputManager {
public:
    enum class Context {
        MAIN_MENU,   // Main menu - before game starts
        GAMEPLAY,    // Normal gameplay - all controls enabled
        MENU,        // Pause menu open - gameplay controls disabled
        CONSOLE,     // Console open - gameplay controls disabled
        INVENTORY,   // Inventory open - mouse enabled, gameplay disabled
        PAUSED       // Generic paused state
    };

    static InputManager& instance();

    // Context management
    void setContext(Context ctx) { m_context = ctx; }
    Context getContext() const { return m_context; }

    // Cursor unlock state (for temporary cursor unlock via key)
    void setCursorUnlocked(bool unlocked) { m_cursorUnlocked = unlocked; }
    bool isCursorUnlocked() const { return m_cursorUnlocked; }

    // Convenience query methods
    bool isGameplayEnabled() const { return m_context == Context::GAMEPLAY && !m_cursorUnlocked; }
    bool canMove() const { return m_context == Context::GAMEPLAY && !m_cursorUnlocked; }
    bool canLook() const { return m_context == Context::GAMEPLAY && !m_cursorUnlocked; }
    bool canInteract() const { return m_context == Context::GAMEPLAY && !m_cursorUnlocked; }
    bool canBreakBlocks() const { return m_context == Context::GAMEPLAY && !m_cursorUnlocked; }
    bool canPlaceBlocks() const { return m_context == Context::GAMEPLAY && !m_cursorUnlocked; }

    // Menu/UI controls always available
    bool canOpenMenu() const { return true; }
    bool canOpenConsole() const { return true; }

private:
    InputManager() = default;
    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;

    Context m_context = Context::GAMEPLAY;
    bool m_cursorUnlocked = false;
};
