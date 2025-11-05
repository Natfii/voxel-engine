#include "console.h"
#include "command_registry.h"
#include "imgui.h"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>

Console::Console(GLFWwindow* window)
    : m_window(window), m_isVisible(false), m_focusInput(false), m_historyIndex(-1), m_scrollToBottom(false), m_suggestionIndex(0) {
    memset(m_inputBuffer, 0, sizeof(m_inputBuffer));

    addMessage("Voxel Engine Console", ConsoleMessageType::INFO);
    addMessage("Type 'help' for a list of commands", ConsoleMessageType::INFO);
}

void Console::render() {
    if (!m_isVisible) {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 displaySize = io.DisplaySize;

    // Console takes up top half of screen
    float consoleHeight = displaySize.y * 0.5f;
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(displaySize.x, consoleHeight), ImGuiCond_Always);

    // Dark transparent background
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("Console", nullptr, flags)) {
        // Check if user clicked outside the console window
        if (ImGui::IsMouseClicked(0) && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
            m_isVisible = false;
        }

        // Messages area
        renderMessages();

        ImGui::Separator();

        // Autocomplete suggestions (render above input so they don't get cut off)
        if (!m_suggestions.empty()) {
            renderSuggestions();
        }

        // Input area
        renderInput();
    }

    ImGui::End();
    ImGui::PopStyleColor(2);
}

void Console::renderMessages() {
    // Reserve space for input and suggestions at bottom (60 pixels total)
    ImGui::BeginChild("ConsoleMessages", ImVec2(0, -60), true);

    for (const auto& msg : m_messages) {
        ImVec4 color;
        switch (msg.type) {
            case ConsoleMessageType::ERROR:
                color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f); // Red
                break;
            case ConsoleMessageType::WARNING:
                color = ImVec4(1.0f, 1.0f, 0.3f, 1.0f); // Yellow
                break;
            case ConsoleMessageType::COMMAND:
                color = ImVec4(0.5f, 0.5f, 1.0f, 1.0f); // Light blue
                break;
            default:
                color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); // White
                break;
        }

        ImGui::TextColored(color, "%s", msg.text.c_str());
    }

    // Scroll to bottom when flag is set (after new message added)
    if (m_scrollToBottom) {
        ImGui::SetScrollHereY(1.0f);
        m_scrollToBottom = false;
    }

    ImGui::EndChild();
}

void Console::renderInput() {
    ImGui::PushItemWidth(-1);

    // Set focus on input when console opens
    if (m_focusInput) {
        ImGui::SetKeyboardFocusHere();
        m_focusInput = false;
    }

    // Input text field
    ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue |
                                    ImGuiInputTextFlags_CallbackHistory |
                                    ImGuiInputTextFlags_CallbackCompletion |
                                    ImGuiInputTextFlags_CallbackEdit;

    bool executeNow = ImGui::InputText("##ConsoleInput", m_inputBuffer, sizeof(m_inputBuffer),
        inputFlags, [](ImGuiInputTextCallbackData* data) -> int {
            Console* console = static_cast<Console*>(data->UserData);
            if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
                if (data->EventKey == ImGuiKey_UpArrow) {
                    console->navigateHistory(1);
                    data->DeleteChars(0, data->BufTextLen);
                    data->InsertChars(0, console->m_inputBuffer);
                } else if (data->EventKey == ImGuiKey_DownArrow) {
                    console->navigateHistory(-1);
                    data->DeleteChars(0, data->BufTextLen);
                    data->InsertChars(0, console->m_inputBuffer);
                }
            } else if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
                // Tab key pressed
                console->autoComplete();
                data->DeleteChars(0, data->BufTextLen);
                data->InsertChars(0, console->m_inputBuffer);
                // Move cursor to end instead of selecting all (prevents accidental overwrite)
                data->CursorPos = data->BufTextLen;
                data->SelectionStart = data->SelectionEnd = data->CursorPos;
            } else if (data->EventFlag == ImGuiInputTextFlags_CallbackEdit) {
                // Input changed, reset suggestion index
                console->m_suggestionIndex = 0;
            }
            return 0;
        }, this);

    ImGui::PopItemWidth();

    // Update autocomplete suggestions based on input (handles commands and arguments)
    std::string currentInput(m_inputBuffer);
    m_suggestions = CommandRegistry::instance().getFullCompletions(currentInput);

    // Execute command when Enter is pressed
    if (executeNow && m_inputBuffer[0] != '\0') {
        executeCommand(m_inputBuffer);
        memset(m_inputBuffer, 0, sizeof(m_inputBuffer));
        m_historyIndex = -1;
        m_suggestions.clear();
        m_focusInput = true; // Keep focus on input
    }
}

void Console::renderSuggestions() {
    ImGui::Text("Suggestions:");
    ImGui::SameLine();

    for (size_t i = 0; i < m_suggestions.size() && i < 5; i++) {
        if (i > 0) ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", m_suggestions[i].c_str());
    }
}

void Console::addMessage(const std::string& message, ConsoleMessageType type) {
    m_messages.push_back({message, type});

    // Limit message history
    if (m_messages.size() > MAX_MESSAGES) {
        m_messages.pop_front();
    }

    // Scroll to bottom when new message is added
    m_scrollToBottom = true;

    // Also print to stdout for debugging
    if (type == ConsoleMessageType::ERROR) {
        std::cerr << message << std::endl;
    } else {
        std::cout << message << std::endl;
    }
}

void Console::executeCommand(const std::string& command) {
    // Add to console output
    addMessage("> " + command, ConsoleMessageType::COMMAND);

    // Add to history
    m_commandHistory.push_back(command);
    if (m_commandHistory.size() > MAX_HISTORY) {
        m_commandHistory.erase(m_commandHistory.begin());
    }

    // Check if this is a .md file path
    if (command.length() > 3 && command.substr(command.length() - 3) == ".md") {
        loadMarkdownFile(command);
        return;
    }

    // Execute the command
    CommandRegistry::instance().executeCommand(command);
}

void Console::loadMarkdownFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        addMessage("Error: Could not open file: " + filepath, ConsoleMessageType::ERROR);
        return;
    }

    addMessage("=== " + filepath + " ===", ConsoleMessageType::INFO);

    std::string line;
    while (std::getline(file, line)) {
        // Simple markdown rendering
        ConsoleMessageType type = ConsoleMessageType::INFO;

        // Headers (lines starting with #)
        if (!line.empty() && line[0] == '#') {
            type = ConsoleMessageType::WARNING; // Use warning color for headers (yellow)
        }

        addMessage(line, type);
    }

    file.close();
    addMessage("=== End of " + filepath + " ===", ConsoleMessageType::INFO);
}

void Console::navigateHistory(int direction) {
    if (m_commandHistory.empty()) {
        return;
    }

    if (direction > 0) {
        // Up arrow - go back in history
        m_historyIndex++;
        if (m_historyIndex >= (int)m_commandHistory.size()) {
            m_historyIndex = m_commandHistory.size() - 1;
        }
    } else {
        // Down arrow - go forward in history
        m_historyIndex--;
        if (m_historyIndex < 0) {
            m_historyIndex = -1;
            memset(m_inputBuffer, 0, sizeof(m_inputBuffer));
            return;
        }
    }

    // Copy history command to input buffer
    if (m_historyIndex >= 0 && m_historyIndex < (int)m_commandHistory.size()) {
        size_t historySize = m_commandHistory.size();
        const std::string& cmd = m_commandHistory[historySize - 1 - m_historyIndex];
        strncpy_s(m_inputBuffer, cmd.c_str(), sizeof(m_inputBuffer) - 1);
    }
}

void Console::autoComplete() {
    if (m_suggestions.empty()) {
        return;
    }

    // Get current suggestion (cycle through with repeated Tab presses)
    const std::string& suggestion = m_suggestions[m_suggestionIndex % m_suggestions.size()];

    // Copy suggestion to input buffer
    strncpy_s(m_inputBuffer, suggestion.c_str(), sizeof(m_inputBuffer) - 1);

    // Move to next suggestion for next Tab press
    m_suggestionIndex++;
    if (m_suggestionIndex >= (int)m_suggestions.size()) {
        m_suggestionIndex = 0;
    }
}
