#pragma once

#include <GLFW/glfw3.h>
#include <string>
#include <vector>
#include <deque>

// Console message types for color coding
enum class ConsoleMessageType {
    INFO,
    WARNING,
    ERROR,
    COMMAND
};

struct ConsoleMessage {
    std::string text;
    ConsoleMessageType type;
};

class Console {
public:
    Console(GLFWwindow* window);

    // Render the console UI
    void render();

    // Toggle console visibility
    void toggle() {
        m_isVisible = !m_isVisible;
        if (m_isVisible) {
            m_focusInput = true;  // Auto-focus input when opening console
        }
    }
    bool isVisible() const { return m_isVisible; }
    void setVisible(bool visible) { m_isVisible = visible; }

    // Add a message to the console output
    void addMessage(const std::string& message, ConsoleMessageType type = ConsoleMessageType::INFO);

    // Execute a command and add it to history
    void executeCommand(const std::string& command);

    // Load and display a markdown file
    void loadMarkdownFile(const std::string& filepath);

private:
    GLFWwindow* m_window;
    bool m_isVisible;

    // Console output
    std::deque<ConsoleMessage> m_messages;
    static const size_t MAX_MESSAGES = 1000;

    // Input
    char m_inputBuffer[256];
    bool m_focusInput;

    // Command history
    std::vector<std::string> m_commandHistory;
    int m_historyIndex;
    static const size_t MAX_HISTORY = 100;

    // Autocomplete
    std::vector<std::string> m_suggestions;
    int m_suggestionIndex;

    // Scroll control
    bool m_scrollToBottom;

    void renderMessages();
    void renderInput();
    void renderSuggestions();

    void navigateHistory(int direction);
    void autoComplete();
};
