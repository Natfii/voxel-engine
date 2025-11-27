/**
 * @file paint_editor.cpp
 * @brief Implementation of 2D Pixel Paint Editor
 */

#include "editor/paint_editor.h"
#include "console_commands.h"
#include "command_registry.h"
#include <algorithm>
#include <cmath>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <stb_image.h>

PaintEditor::PaintEditor() {
    // Initialize canvas with white pixels
    m_pixels.resize(CANVAS_WIDTH * CANVAS_HEIGHT, 0xFFFFFFFF);

    // Initialize preset palette colors (common pixel art colors)
    m_presetColors = {
        0x000000FF,  // Black
        0xFFFFFFFF,  // White
        0x0000FFFF,  // Red
        0x00FF00FF,  // Green
        0xFF0000FF,  // Blue
        0x00FFFFFF,  // Yellow
        0xFFFF00FF,  // Cyan
        0xFF00FFFF,  // Magenta
        0x000080FF,  // Dark Red
        0x008000FF,  // Dark Green
        0x800000FF,  // Dark Blue
        0x808080FF,  // Gray
        0xC0C0C0FF,  // Light Gray
        0x0080FFFF,  // Orange
        0x8000FFFF,  // Pink
        0x008080FF   // Brown
    };
}

PaintEditor::~PaintEditor() {
}

void PaintEditor::render() {
    if (!m_isOpen) return;

    // Process keyboard shortcuts
    processShortcuts();

    ImGui::SetNextWindowSize(ImVec2(800, 700), ImGuiCond_FirstUseEver);

    // No close button in editor-only mode (debug 2)
    // NoScrollbar prevents scroll from interfering with zoom
    ImGui::Begin("2D Paint Editor", ConsoleCommands::isEditorOnlyMode() ? nullptr : &m_isOpen,
        ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // Menu bar
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Canvas", "Ctrl+N")) {
                newCanvas();
            }
            if (ImGui::MenuItem("Load Image...", "Ctrl+O")) {
                m_fileBrowserMode = FileBrowser::Mode::OPEN;
                m_fileBrowser.open(FileBrowser::Mode::OPEN, "Load Image", {".png"}, "assets/paint");
            }
            if (ImGui::MenuItem("Save Image", "Ctrl+S")) {
                if (!m_currentFilePath.empty()) {
                    saveToFile(m_currentFilePath);
                } else {
                    m_fileBrowserMode = FileBrowser::Mode::SAVE;
                    m_fileBrowser.open(FileBrowser::Mode::SAVE, "Save Image", {".png"}, "assets/paint");
                }
            }
            if (ImGui::MenuItem("Save Image As...", "Ctrl+Shift+S")) {
                m_fileBrowserMode = FileBrowser::Mode::SAVE;
                m_fileBrowser.open(FileBrowser::Mode::SAVE, "Save Image As", {".png"}, "assets/paint");
            }
            ImGui::Separator();
            // Save & Reload - disabled in debug 2 (no 3D world to reload into)
            bool canReload = !ConsoleCommands::isEditorOnlyMode();
            if (!canReload) {
                ImGui::BeginDisabled();
            }
            if (ImGui::MenuItem("Save & Reload", nullptr, false, canReload)) {
                if (!m_currentFilePath.empty()) {
                    if (saveToFile(m_currentFilePath)) {
                        // Trigger engine reload command
                        CommandRegistry::instance().executeCommand("reload");
                    }
                } else {
                    // Need to save first
                    m_fileBrowserMode = FileBrowser::Mode::SAVE;
                    m_fileBrowser.open(FileBrowser::Mode::SAVE, "Save Image", {".png"}, "assets/paint");
                }
            }
            if (!canReload) {
                ImGui::EndDisabled();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                if (canReload) {
                    ImGui::SetTooltip("Save and reload textures in-game");
                } else {
                    ImGui::SetTooltip("Disabled in editor-only mode (no 3D world)");
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !m_undoStack.empty())) {
                undo();
            }
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !m_redoStack.empty())) {
                redo();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Clear Canvas")) {
                beginStroke();
                for (int i = 0; i < CANVAS_WIDTH * CANVAS_HEIGHT; i++) {
                    if (m_pixels[i] != 0xFFFFFFFF) {
                        recordPixelChange(i, m_pixels[i]);
                        m_pixels[i] = 0xFFFFFFFF;
                    }
                }
                endStroke();
                m_canvasDirty = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::Checkbox("Show Grid", &m_showGrid);
            ImGui::Checkbox("Mirror X", &m_mirrorX);
            ImGui::Checkbox("Mirror Y", &m_mirrorY);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Zoom")) {
                m_zoom = 8.0f;
                m_panOffset = {0, 0};
            }
            if (ImGui::MenuItem("Keyboard Shortcuts", "?")) {
                m_showShortcuts = !m_showShortcuts;
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    // Main layout - toolbar on left, canvas in center, color bar at bottom
    float colorBarHeight = 50.0f;
    float availHeight = ImGui::GetContentRegionAvail().y - colorBarHeight - 25.0f; // Leave room for status

    // Top section: toolbar + canvas
    ImGui::BeginChild("TopSection", ImVec2(0, availHeight), false);
    renderToolbar();
    ImGui::SameLine();
    renderCanvas();
    ImGui::EndChild();

    // Bottom: MSPaint-style color palette bar
    renderColorBar();

    // Status bar at very bottom
    renderStatusBar();

    // Shortcuts overlay
    if (m_showShortcuts) {
        renderShortcutsOverlay();
    }

    // File browser handling
    if (m_fileBrowser.render()) {
        std::string result = m_fileBrowser.getSelectedPath();
        if (m_fileBrowserMode == FileBrowser::Mode::OPEN) {
            loadFromFile(result);
        } else {
            // Ensure .png extension
            if (result.length() < 4 || result.substr(result.length() - 4) != ".png") {
                result += ".png";
            }
            saveToFile(result);
        }
    }

    ImGui::End();
}

void PaintEditor::renderToolbar() {
    ImGui::BeginChild("Toolbar", ImVec2(80, 0), true);

    ImGui::Text("Tools");
    ImGui::Separator();

    // Tool buttons
    auto toolButton = [this](const char* label, PaintTool tool, const char* tooltip) {
        bool selected = (m_currentTool == tool);
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
        if (ImGui::Button(label, ImVec2(64, 24))) {
            m_currentTool = tool;
        }
        if (selected) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tooltip);
        }
    };

    toolButton("Brush", PaintTool::Brush, "Paint pixels with current color\nLeft-click: Primary color\nRight-click: Secondary color\nShortcut: B");
    toolButton("Eraser", PaintTool::Eraser, "Erase pixels to transparent\nShortcut: E");
    toolButton("Fill", PaintTool::Fill, "Flood fill connected area\nClick to fill with primary color\nShortcut: F");
    toolButton("Picker", PaintTool::ColorPicker, "Pick color from canvas\nLeft-click: Set primary\nRight-click: Set secondary\nShortcut: P");
    toolButton("Line", PaintTool::Line, "Draw straight line\nClick and drag to draw\nShortcut: L");
    toolButton("Rect", PaintTool::Rectangle, "Draw rectangle outline\nClick and drag corners\nShortcut: R");
    toolButton("Circle", PaintTool::Circle, "Draw circle outline\nClick center, drag radius\nShortcut: C");

    ImGui::Separator();
    ImGui::Text("Size");
    ImGui::SliderInt("##BrushSize", &m_brushSize, 1, 8);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Brush size (1-8 pixels)");
    }

    ImGui::Separator();
    ImGui::Text("Options");
    ImGui::Checkbox("Grid", &m_showGrid);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Show pixel grid overlay\nHelps with precise pixel placement\nShortcut: G");
    }
    ImGui::Checkbox("MirrorX", &m_mirrorX);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Mirror drawing horizontally\nDraws on both sides of center\nShortcut: M");
    }
    ImGui::Checkbox("MirrorY", &m_mirrorY);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Mirror drawing vertically\nDraws above and below center");
    }

    ImGui::Separator();
    ImGui::Text("Zoom");
    ImGui::Text("%.0fx", m_zoom);
    if (ImGui::Button("+", ImVec2(28, 24))) {
        m_zoom = std::min(m_zoom * 2.0f, 16.0f);
    }
    ImGui::SameLine();
    if (ImGui::Button("-", ImVec2(28, 24))) {
        m_zoom = std::max(m_zoom / 2.0f, 1.0f);
    }

    ImGui::EndChild();
}

void PaintEditor::renderPalette() {
    ImGui::BeginChild("Palette", ImVec2(180, 0), true);

    // === MSPaint-style current color display ===
    float r, g, b, a;

    // Large primary/secondary color boxes (MSPaint style - overlapping squares)
    ImVec2 boxPos = ImGui::GetCursorScreenPos();

    // Secondary color box (behind, offset)
    ABGRToColor(m_secondaryColor, r, g, b, a);
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(boxPos.x + 20, boxPos.y + 20),
        ImVec2(boxPos.x + 55, boxPos.y + 55),
        IM_COL32(static_cast<int>(r*255), static_cast<int>(g*255), static_cast<int>(b*255), static_cast<int>(a*255)));
    ImGui::GetWindowDrawList()->AddRect(
        ImVec2(boxPos.x + 20, boxPos.y + 20),
        ImVec2(boxPos.x + 55, boxPos.y + 55),
        IM_COL32(100, 100, 100, 255));

    // Primary color box (front)
    ABGRToColor(m_primaryColor, r, g, b, a);
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(boxPos.x + 5, boxPos.y + 5),
        ImVec2(boxPos.x + 40, boxPos.y + 40),
        IM_COL32(static_cast<int>(r*255), static_cast<int>(g*255), static_cast<int>(b*255), static_cast<int>(a*255)));
    ImGui::GetWindowDrawList()->AddRect(
        ImVec2(boxPos.x + 5, boxPos.y + 5),
        ImVec2(boxPos.x + 40, boxPos.y + 40),
        IM_COL32(255, 255, 255, 255));

    // Invisible button for the color display area
    ImGui::InvisibleButton("##colorDisplay", ImVec2(60, 60));

    ImGui::SameLine();

    // Color picker buttons with full color picker popup
    ImGui::BeginGroup();

    // Static color arrays for picker popups (persist across frames)
    static float primaryPickerColor[4] = {0, 0, 0, 1};
    static float secondaryPickerColor[4] = {1, 1, 1, 1};
    static bool primaryPickerOpen = false;
    static bool secondaryPickerOpen = false;

    // Primary color picker - clicking opens full color dialog
    ABGRToColor(m_primaryColor, r, g, b, a);
    if (ImGui::ColorButton("##PrimaryBtn", ImVec4(r, g, b, a), ImGuiColorEditFlags_AlphaPreview, ImVec2(24, 24))) {
        // Initialize picker with current color when opening
        primaryPickerColor[0] = r;
        primaryPickerColor[1] = g;
        primaryPickerColor[2] = b;
        primaryPickerColor[3] = a;
        ImGui::OpenPopup("PrimaryColorPicker");
        primaryPickerOpen = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Left-click color\nClick to edit");

    // Primary color picker popup
    if (ImGui::BeginPopup("PrimaryColorPicker")) {
        ImGui::Text("Edit Primary Color");
        ImGui::Separator();
        ImGui::ColorPicker4("##PrimaryPicker", primaryPickerColor,
            ImGuiColorEditFlags_AlphaBar |
            ImGuiColorEditFlags_AlphaPreview |
            ImGuiColorEditFlags_PickerHueWheel |
            ImGuiColorEditFlags_DisplayRGB |
            ImGuiColorEditFlags_DisplayHSV |
            ImGuiColorEditFlags_InputRGB);
        // Always update primary color from picker
        m_primaryColor = colorToABGR(primaryPickerColor[0], primaryPickerColor[1], primaryPickerColor[2], primaryPickerColor[3]);

        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(80, 0))) {
            addRecentColor(m_primaryColor);
            ImGui::CloseCurrentPopup();
            primaryPickerOpen = false;
        }
        ImGui::EndPopup();
    } else {
        primaryPickerOpen = false;
    }

    // Secondary color picker
    ABGRToColor(m_secondaryColor, r, g, b, a);
    if (ImGui::ColorButton("##SecondaryBtn", ImVec4(r, g, b, a), ImGuiColorEditFlags_AlphaPreview, ImVec2(24, 24))) {
        // Initialize picker with current color when opening
        secondaryPickerColor[0] = r;
        secondaryPickerColor[1] = g;
        secondaryPickerColor[2] = b;
        secondaryPickerColor[3] = a;
        ImGui::OpenPopup("SecondaryColorPicker");
        secondaryPickerOpen = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Right-click color\nClick to edit");

    // Secondary color picker popup
    if (ImGui::BeginPopup("SecondaryColorPicker")) {
        ImGui::Text("Edit Secondary Color");
        ImGui::Separator();
        ImGui::ColorPicker4("##SecondaryPicker", secondaryPickerColor,
            ImGuiColorEditFlags_AlphaBar |
            ImGuiColorEditFlags_AlphaPreview |
            ImGuiColorEditFlags_PickerHueWheel |
            ImGuiColorEditFlags_DisplayRGB |
            ImGuiColorEditFlags_DisplayHSV |
            ImGuiColorEditFlags_InputRGB);
        // Always update secondary color from picker
        m_secondaryColor = colorToABGR(secondaryPickerColor[0], secondaryPickerColor[1], secondaryPickerColor[2], secondaryPickerColor[3]);

        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(80, 0))) {
            ImGui::CloseCurrentPopup();
            secondaryPickerOpen = false;
        }
        ImGui::EndPopup();
    } else {
        secondaryPickerOpen = false;
    }

    if (ImGui::SmallButton("Swap")) {
        std::swap(m_primaryColor, m_secondaryColor);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Swap colors (X)");
    ImGui::EndGroup();

    ImGui::Separator();

    // === MSPaint-style color palette grid (28 colors in 2 rows) ===
    ImGui::Text("Palette");

    // MSPaint classic colors (ABGR format)
    static const uint32_t mspaintColors[] = {
        // Row 1: Basic colors
        0xFF000000, 0xFF7F7F7F, 0xFF00007F, 0xFF007F00,  // Black, Gray, Maroon, Green
        0xFF7F0000, 0xFF7F007F, 0xFF007F7F, 0xFFC0C0C0,  // Navy, Purple, Teal, Silver
        0xFF3F3F3F, 0xFFFF0000, 0xFF00FF00, 0xFF00FFFF,  // DkGray, Red, Lime, Yellow
        0xFFFF00FF, 0xFFFFFF00, 0xFF0000FF,              // Blue, Magenta, Cyan
        // Row 2: More colors
        0xFFFFFFFF, 0xFF9F9F9F, 0xFF00009F, 0xFF009F00,  // White, LtGray, DkRed, DkGreen
        0xFF9F0000, 0xFF9F009F, 0xFF009F9F, 0xFFFFCFCF,  // DkBlue, DkMagenta, DkCyan, Pink
        0xFF7FFFFF, 0xFFFFFF7F, 0xFF7FFF7F, 0xFFFF7F7F,  // LtYellow, LtBlue, LtGreen, LtRed
        0xFFFF7FFF, 0xFF7FFFFF                           // LtMagenta, LtCyan
    };
    int numColors = sizeof(mspaintColors) / sizeof(mspaintColors[0]);
    int cols = 14;
    float buttonSize = 18.0f;

    for (int i = 0; i < numColors; i++) {
        ABGRToColor(mspaintColors[i], r, g, b, a);
        ImVec4 col(r, g, b, a);

        ImGui::PushID(i);
        if (ImGui::ColorButton("##mspaint", col, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder, ImVec2(buttonSize, buttonSize))) {
            // Left click = primary, check for right click too
            m_primaryColor = mspaintColors[i];
            addRecentColor(m_primaryColor);
        }
        // Right-click to set secondary color
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            m_secondaryColor = mspaintColors[i];
        }
        ImGui::PopID();

        if ((i + 1) % cols != 0 && i < numColors - 1) {
            ImGui::SameLine(0, 2);
        }
    }

    ImGui::Separator();

    // === Custom preset colors (editable) ===
    ImGui::Text("Custom");

    cols = 8;
    for (int i = 0; i < (int)m_presetColors.size() && i < 16; i++) {
        ABGRToColor(m_presetColors[i], r, g, b, a);
        ImVec4 col(r, g, b, a);

        ImGui::PushID(200 + i);
        if (ImGui::ColorButton("##custom", col, ImGuiColorEditFlags_NoTooltip, ImVec2(16, 16))) {
            m_primaryColor = m_presetColors[i];
            addRecentColor(m_primaryColor);
        }
        // Right-click to save current primary color to this slot
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            m_presetColors[i] = m_primaryColor;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Left: Use color\nRight: Save current color");
        }
        ImGui::PopID();

        if ((i + 1) % cols != 0) ImGui::SameLine(0, 2);
    }

    // Recent colors
    if (!m_recentColors.empty()) {
        ImGui::Separator();
        ImGui::Text("Recent");

        for (int i = 0; i < (int)m_recentColors.size(); i++) {
            ABGRToColor(m_recentColors[i], r, g, b, a);
            ImVec4 col(r, g, b, a);

            ImGui::PushID(300 + i);
            if (ImGui::ColorButton("##recent", col, ImGuiColorEditFlags_NoTooltip, ImVec2(16, 16))) {
                m_primaryColor = m_recentColors[i];
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                m_secondaryColor = m_recentColors[i];
            }
            ImGui::PopID();

            if ((i + 1) % cols != 0) ImGui::SameLine(0, 2);
        }
    }

    ImGui::EndChild();
}

void PaintEditor::renderColorBar() {
    // MSPaint-style horizontal color bar at bottom
    ImGui::BeginChild("ColorBar", ImVec2(0, 45), true);

    float r, g, b, a;

    // Left side: Primary/Secondary color boxes (overlapping squares like MSPaint)
    ImVec2 boxPos = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // Helper to draw checkerboard background for transparency preview
    auto drawCheckerboard = [drawList](float x1, float y1, float x2, float y2) {
        int gridSize = 6;
        for (int gy = 0; gy * gridSize < (y2 - y1); gy++) {
            for (int gx = 0; gx * gridSize < (x2 - x1); gx++) {
                bool light = ((gx + gy) % 2) == 0;
                ImU32 col = light ? IM_COL32(255, 255, 255, 255) : IM_COL32(180, 180, 180, 255);
                float px1 = x1 + gx * gridSize;
                float py1 = y1 + gy * gridSize;
                float px2 = std::min(px1 + gridSize, x2);
                float py2 = std::min(py1 + gridSize, y2);
                drawList->AddRectFilled(ImVec2(px1, py1), ImVec2(px2, py2), col);
            }
        }
    };

    // Secondary color box (behind, offset down-right) - draw checkerboard first
    drawCheckerboard(boxPos.x + 16, boxPos.y + 16, boxPos.x + 40, boxPos.y + 40);
    ABGRToColor(m_secondaryColor, r, g, b, a);
    drawList->AddRectFilled(
        ImVec2(boxPos.x + 16, boxPos.y + 16),
        ImVec2(boxPos.x + 40, boxPos.y + 40),
        IM_COL32(static_cast<int>(r*255), static_cast<int>(g*255), static_cast<int>(b*255), static_cast<int>(a*255)));
    drawList->AddRect(
        ImVec2(boxPos.x + 16, boxPos.y + 16),
        ImVec2(boxPos.x + 40, boxPos.y + 40),
        IM_COL32(80, 80, 80, 255));

    // Primary color box (front, top-left) - draw checkerboard first
    drawCheckerboard(boxPos.x + 2, boxPos.y + 2, boxPos.x + 26, boxPos.y + 26);
    ABGRToColor(m_primaryColor, r, g, b, a);
    drawList->AddRectFilled(
        ImVec2(boxPos.x + 2, boxPos.y + 2),
        ImVec2(boxPos.x + 26, boxPos.y + 26),
        IM_COL32(static_cast<int>(r*255), static_cast<int>(g*255), static_cast<int>(b*255), static_cast<int>(a*255)));
    drawList->AddRect(
        ImVec2(boxPos.x + 2, boxPos.y + 2),
        ImVec2(boxPos.x + 26, boxPos.y + 26),
        IM_COL32(255, 255, 255, 255));

    // Invisible button for color area interaction
    if (ImGui::InvisibleButton("##colorBoxes", ImVec2(45, 42))) {
        // Swap colors on click
        std::swap(m_primaryColor, m_secondaryColor);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to swap colors");

    ImGui::SameLine();
    ImGui::Dummy(ImVec2(10, 0)); // Spacer
    ImGui::SameLine();

    // MSPaint classic color palette (horizontal grid)
    static const uint32_t mspaintColors[] = {
        // Row 1 (top)
        0xFF000000, 0xFF7F7F7F, 0xFF00007F, 0xFF007F7F, 0xFF007F00, 0xFF7F7F00, 0xFF7F0000, 0xFF7F007F,
        0xFF003F7F, 0xFF007F3F, 0xFF3F7F00, 0xFF7F3F00, 0xFF3F007F, 0xFF00007F, 0xFF000000, 0xFF7F7F7F,
        // Row 2 (bottom)
        0xFFFFFFFF, 0xFFC0C0C0, 0xFF0000FF, 0xFF00FFFF, 0xFF00FF00, 0xFFFFFF00, 0xFFFF0000, 0xFFFF00FF,
        0xFF007FFF, 0xFF00FF7F, 0xFF7FFF00, 0xFFFF7F00, 0xFF7F00FF, 0xFF0000FF, 0xFFFFFFFF, 0xFFC0C0C0
    };

    int numColors = sizeof(mspaintColors) / sizeof(mspaintColors[0]);
    int cols = 16; // 16 colors per row
    float buttonSize = 16.0f;

    ImGui::BeginGroup();
    for (int i = 0; i < numColors; i++) {
        ABGRToColor(mspaintColors[i], r, g, b, a);

        ImGui::PushID(i + 1000);
        if (ImGui::ColorButton("##pal", ImVec4(r, g, b, a),
            ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder, ImVec2(buttonSize, buttonSize))) {
            m_primaryColor = mspaintColors[i];
            addRecentColor(m_primaryColor);
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            m_secondaryColor = mspaintColors[i];
        }
        ImGui::PopID();

        // Layout: 16 colors then newline
        if ((i + 1) % cols != 0) {
            ImGui::SameLine(0, 1);
        }
    }
    ImGui::EndGroup();

    ImGui::SameLine();
    ImGui::Dummy(ImVec2(20, 0)); // Spacer
    ImGui::SameLine();

    // Edit Colors button to open full color picker
    ImGui::BeginGroup();
    static float editColor[4] = {0, 0, 0, 1};

    if (ImGui::Button("Edit\nColors", ImVec2(50, 35))) {
        ABGRToColor(m_primaryColor, editColor[0], editColor[1], editColor[2], editColor[3]);
        ImGui::OpenPopup("EditColorPopup");
    }

    if (ImGui::BeginPopup("EditColorPopup")) {
        ImGui::Text("Edit Color");
        ImGui::Separator();
        ImGui::ColorPicker4("##editpicker", editColor,
            ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_PickerHueWheel);
        m_primaryColor = colorToABGR(editColor[0], editColor[1], editColor[2], editColor[3]);
        if (ImGui::Button("OK", ImVec2(80, 0))) {
            addRecentColor(m_primaryColor);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::EndGroup();

    ImGui::EndChild();
}

void PaintEditor::renderCanvas() {
    ImVec2 canvasSize(CANVAS_WIDTH * m_zoom, CANVAS_HEIGHT * m_zoom);

    ImGui::BeginChild("CanvasArea", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // Store the base position before applying pan offset
    ImVec2 baseCanvasPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasPos = baseCanvasPos;
    canvasPos.x += m_panOffset.x;
    canvasPos.y += m_panOffset.y;

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // Get child window size for clipping
    ImVec2 childSize = ImGui::GetContentRegionAvail();
    drawList->PushClipRect(baseCanvasPos, ImVec2(baseCanvasPos.x + childSize.x, baseCanvasPos.y + childSize.y), true);

    // Draw checkerboard background for transparency
    for (int y = 0; y < CANVAS_HEIGHT; y++) {
        for (int x = 0; x < CANVAS_WIDTH; x++) {
            bool dark = ((x + y) % 2 == 0);
            ImU32 bgColor = dark ? IM_COL32(200, 200, 200, 255) : IM_COL32(255, 255, 255, 255);

            ImVec2 p0(canvasPos.x + x * m_zoom, canvasPos.y + y * m_zoom);
            ImVec2 p1(p0.x + m_zoom, p0.y + m_zoom);
            drawList->AddRectFilled(p0, p1, bgColor);
        }
    }

    // Draw pixels
    for (int y = 0; y < CANVAS_HEIGHT; y++) {
        for (int x = 0; x < CANVAS_WIDTH; x++) {
            uint32_t pixel = m_pixels[pixelIndex(x, y)];
            uint8_t a = (pixel >> 24) & 0xFF;
            uint8_t b = (pixel >> 16) & 0xFF;
            uint8_t g = (pixel >> 8) & 0xFF;
            uint8_t r = pixel & 0xFF;

            if (a > 0) {
                ImVec2 p0(canvasPos.x + x * m_zoom, canvasPos.y + y * m_zoom);
                ImVec2 p1(p0.x + m_zoom, p0.y + m_zoom);
                drawList->AddRectFilled(p0, p1, IM_COL32(r, g, b, a));
            }
        }
    }

    // Draw grid
    if (m_showGrid && m_zoom >= 4.0f) {
        ImU32 gridColor = IM_COL32(100, 100, 100, 100);
        for (int x = 0; x <= CANVAS_WIDTH; x++) {
            float px = canvasPos.x + x * m_zoom;
            drawList->AddLine(ImVec2(px, canvasPos.y), ImVec2(px, canvasPos.y + canvasSize.y), gridColor);
        }
        for (int y = 0; y <= CANVAS_HEIGHT; y++) {
            float py = canvasPos.y + y * m_zoom;
            drawList->AddLine(ImVec2(canvasPos.x, py), ImVec2(canvasPos.x + canvasSize.x, py), gridColor);
        }
    }

    // Draw mirror guides
    if (m_mirrorX) {
        float midX = canvasPos.x + (CANVAS_WIDTH / 2) * m_zoom;
        drawList->AddLine(ImVec2(midX, canvasPos.y), ImVec2(midX, canvasPos.y + canvasSize.y), IM_COL32(255, 0, 0, 150), 2.0f);
    }
    if (m_mirrorY) {
        float midY = canvasPos.y + (CANVAS_HEIGHT / 2) * m_zoom;
        drawList->AddLine(ImVec2(canvasPos.x, midY), ImVec2(canvasPos.x + canvasSize.x, midY), IM_COL32(255, 0, 0, 150), 2.0f);
    }

    // Canvas border
    drawList->AddRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), IM_COL32(80, 80, 80, 255), 0, 0, 2.0f);

    // Handle mouse input
    ImVec2 mousePos = ImGui::GetIO().MousePos;
    glm::ivec2 canvasCoord = screenToCanvas(mousePos, canvasPos);

    // Invisible button for input
    ImGui::SetCursorScreenPos(canvasPos);
    ImGui::InvisibleButton("canvas_input", canvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);

    bool isHovered = ImGui::IsItemHovered();
    bool leftDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    bool rightDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    bool middleDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    bool leftClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    bool leftReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);

    // Middle mouse for panning
    if (middleDown && isHovered) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        m_panOffset.x += delta.x;
        m_panOffset.y += delta.y;
    }

    // Scroll wheel for zoom
    if (isHovered) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0) {
            float oldZoom = m_zoom;
            if (wheel > 0) {
                m_zoom = std::min(m_zoom * 2.0f, 16.0f);
            } else {
                m_zoom = std::max(m_zoom / 2.0f, 1.0f);
            }
            // Zoom toward cursor - keep the point under the mouse fixed
            if (m_zoom != oldZoom) {
                // Calculate mouse position relative to canvas (accounting for current pan)
                float relX = mousePos.x - canvasPos.x;
                float relY = mousePos.y - canvasPos.y;

                // Scale the relative position and adjust pan to keep same point under cursor
                float scale = m_zoom / oldZoom;
                m_panOffset.x -= relX * (scale - 1.0f);
                m_panOffset.y -= relY * (scale - 1.0f);
            }
        }
    }

    // Tool-specific input handling
    if (isHovered && inBounds(canvasCoord.x, canvasCoord.y)) {
        // Draw cursor preview
        ImVec2 cursorPos(canvasPos.x + canvasCoord.x * m_zoom, canvasPos.y + canvasCoord.y * m_zoom);
        ImVec2 cursorSize(m_brushSize * m_zoom, m_brushSize * m_zoom);
        drawList->AddRect(cursorPos, ImVec2(cursorPos.x + cursorSize.x, cursorPos.y + cursorSize.y), IM_COL32(255, 255, 0, 200), 0, 0, 2.0f);

        switch (m_currentTool) {
            case PaintTool::Brush:
                if (leftDown) {
                    if (!m_strokeInProgress) beginStroke();
                    paintBrush(canvasCoord.x, canvasCoord.y, m_primaryColor);
                } else if (rightDown) {
                    if (!m_strokeInProgress) beginStroke();
                    paintBrush(canvasCoord.x, canvasCoord.y, m_secondaryColor);
                } else if (m_strokeInProgress) {
                    endStroke();
                }
                break;

            case PaintTool::Eraser:
                if (leftDown || rightDown) {
                    if (!m_strokeInProgress) beginStroke();
                    for (int dy = 0; dy < m_brushSize; dy++) {
                        for (int dx = 0; dx < m_brushSize; dx++) {
                            erasePixel(canvasCoord.x + dx, canvasCoord.y + dy);
                        }
                    }
                } else if (m_strokeInProgress) {
                    endStroke();
                }
                break;

            case PaintTool::Fill:
                if (leftClicked) {
                    beginStroke();
                    floodFill(canvasCoord.x, canvasCoord.y, m_primaryColor);
                    endStroke();
                }
                break;

            case PaintTool::ColorPicker:
                if (leftClicked) {
                    m_primaryColor = pickColor(canvasCoord.x, canvasCoord.y);
                    addRecentColor(m_primaryColor);
                } else if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    m_secondaryColor = pickColor(canvasCoord.x, canvasCoord.y);
                }
                break;

            case PaintTool::Line:
            case PaintTool::Rectangle:
            case PaintTool::Circle:
                if (leftClicked) {
                    m_drawingShape = true;
                    m_shapeStart = canvasCoord;
                    m_shapeEnd = canvasCoord;
                }
                if (m_drawingShape && leftDown) {
                    m_shapeEnd = canvasCoord;
                }
                if (m_drawingShape && leftReleased) {
                    m_drawingShape = false;
                    beginStroke();
                    if (m_currentTool == PaintTool::Line) {
                        drawLine(m_shapeStart.x, m_shapeStart.y, m_shapeEnd.x, m_shapeEnd.y, m_primaryColor);
                    } else if (m_currentTool == PaintTool::Rectangle) {
                        drawRectangle(m_shapeStart.x, m_shapeStart.y, m_shapeEnd.x, m_shapeEnd.y, m_primaryColor, false);
                    } else if (m_currentTool == PaintTool::Circle) {
                        int radius = (int)std::sqrt(std::pow(m_shapeEnd.x - m_shapeStart.x, 2) + std::pow(m_shapeEnd.y - m_shapeStart.y, 2));
                        drawCircle(m_shapeStart.x, m_shapeStart.y, radius, m_primaryColor, false);
                    }
                    endStroke();
                }
                // Draw preview
                if (m_drawingShape) {
                    ImU32 previewColor = IM_COL32(255, 255, 0, 150);
                    if (m_currentTool == PaintTool::Line) {
                        ImVec2 p0(canvasPos.x + m_shapeStart.x * m_zoom + m_zoom/2, canvasPos.y + m_shapeStart.y * m_zoom + m_zoom/2);
                        ImVec2 p1(canvasPos.x + m_shapeEnd.x * m_zoom + m_zoom/2, canvasPos.y + m_shapeEnd.y * m_zoom + m_zoom/2);
                        drawList->AddLine(p0, p1, previewColor, 2.0f);
                    } else if (m_currentTool == PaintTool::Rectangle) {
                        ImVec2 p0(canvasPos.x + std::min(m_shapeStart.x, m_shapeEnd.x) * m_zoom, canvasPos.y + std::min(m_shapeStart.y, m_shapeEnd.y) * m_zoom);
                        ImVec2 p1(canvasPos.x + (std::max(m_shapeStart.x, m_shapeEnd.x) + 1) * m_zoom, canvasPos.y + (std::max(m_shapeStart.y, m_shapeEnd.y) + 1) * m_zoom);
                        drawList->AddRect(p0, p1, previewColor, 0, 0, 2.0f);
                    } else if (m_currentTool == PaintTool::Circle) {
                        int radius = (int)std::sqrt(std::pow(m_shapeEnd.x - m_shapeStart.x, 2) + std::pow(m_shapeEnd.y - m_shapeStart.y, 2));
                        ImVec2 center(canvasPos.x + m_shapeStart.x * m_zoom + m_zoom/2, canvasPos.y + m_shapeStart.y * m_zoom + m_zoom/2);
                        drawList->AddCircle(center, radius * m_zoom, previewColor, 0, 2.0f);
                    }
                }
                break;
        }
    }

    // Pop the clip rect we pushed at the start
    drawList->PopClipRect();

    ImGui::EndChild();
}

void PaintEditor::renderStatusBar() {
    ImGui::Separator();

    ImVec2 mousePos = ImGui::GetIO().MousePos;
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    canvasPos.x += m_panOffset.x;
    canvasPos.y += m_panOffset.y;
    glm::ivec2 coord = screenToCanvas(mousePos, canvasPos);

    std::string toolName;
    switch (m_currentTool) {
        case PaintTool::Brush: toolName = "Brush"; break;
        case PaintTool::Eraser: toolName = "Eraser"; break;
        case PaintTool::Fill: toolName = "Fill"; break;
        case PaintTool::ColorPicker: toolName = "Color Picker"; break;
        case PaintTool::Line: toolName = "Line"; break;
        case PaintTool::Rectangle: toolName = "Rectangle"; break;
        case PaintTool::Circle: toolName = "Circle"; break;
    }

    ImGui::Text("Canvas: 64x64 | Tool: %s | Size: %d | Pos: (%d, %d) | Zoom: %.0fx | %s",
                toolName.c_str(), m_brushSize,
                inBounds(coord.x, coord.y) ? coord.x : -1,
                inBounds(coord.x, coord.y) ? coord.y : -1,
                m_zoom,
                m_modified ? "Modified" : "Saved");
}

void PaintEditor::renderShortcutsOverlay() {
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2, ImGui::GetIO().DisplaySize.y / 2), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::Begin("Keyboard Shortcuts", &m_showShortcuts, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);

    ImGui::Text("Tools:");
    ImGui::BulletText("B - Brush");
    ImGui::BulletText("E - Eraser");
    ImGui::BulletText("F - Fill");
    ImGui::BulletText("P - Color Picker");
    ImGui::BulletText("L - Line");
    ImGui::BulletText("R - Rectangle");
    ImGui::BulletText("C - Circle");

    ImGui::Separator();
    ImGui::Text("Actions:");
    ImGui::BulletText("Ctrl+Z - Undo");
    ImGui::BulletText("Ctrl+Y - Redo");
    ImGui::BulletText("Ctrl+S - Save");
    ImGui::BulletText("Ctrl+O - Open");
    ImGui::BulletText("Ctrl+N - New");

    ImGui::Separator();
    ImGui::Text("View:");
    ImGui::BulletText("G - Toggle Grid");
    ImGui::BulletText("M - Toggle Mirror X");
    ImGui::BulletText("X - Swap Colors");
    ImGui::BulletText("Scroll - Zoom");
    ImGui::BulletText("Middle Drag - Pan");
    ImGui::BulletText("? - Show/Hide Shortcuts");

    ImGui::End();
}

void PaintEditor::paintPixel(int x, int y, uint32_t color) {
    if (!inBounds(x, y)) return;

    int idx = pixelIndex(x, y);
    if (m_pixels[idx] != color) {
        recordPixelChange(idx, m_pixels[idx]);
        m_pixels[idx] = color;
        m_canvasDirty = true;
        m_modified = true;
    }

    // Mirror painting
    if (m_mirrorX) {
        int mirrorX = CANVAS_WIDTH - 1 - x;
        if (mirrorX != x && inBounds(mirrorX, y)) {
            int mirrorIdx = pixelIndex(mirrorX, y);
            if (m_pixels[mirrorIdx] != color) {
                recordPixelChange(mirrorIdx, m_pixels[mirrorIdx]);
                m_pixels[mirrorIdx] = color;
            }
        }
    }
    if (m_mirrorY) {
        int mirrorY = CANVAS_HEIGHT - 1 - y;
        if (mirrorY != y && inBounds(x, mirrorY)) {
            int mirrorIdx = pixelIndex(x, mirrorY);
            if (m_pixels[mirrorIdx] != color) {
                recordPixelChange(mirrorIdx, m_pixels[mirrorIdx]);
                m_pixels[mirrorIdx] = color;
            }
        }
    }
    if (m_mirrorX && m_mirrorY) {
        int mirrorX = CANVAS_WIDTH - 1 - x;
        int mirrorY = CANVAS_HEIGHT - 1 - y;
        if (mirrorX != x && mirrorY != y && inBounds(mirrorX, mirrorY)) {
            int mirrorIdx = pixelIndex(mirrorX, mirrorY);
            if (m_pixels[mirrorIdx] != color) {
                recordPixelChange(mirrorIdx, m_pixels[mirrorIdx]);
                m_pixels[mirrorIdx] = color;
            }
        }
    }
}

void PaintEditor::paintBrush(int x, int y, uint32_t color) {
    for (int dy = 0; dy < m_brushSize; dy++) {
        for (int dx = 0; dx < m_brushSize; dx++) {
            paintPixel(x + dx, y + dy, color);
        }
    }
}

void PaintEditor::erasePixel(int x, int y) {
    paintPixel(x, y, 0x00000000);  // Transparent
}

void PaintEditor::floodFill(int x, int y, uint32_t newColor) {
    if (!inBounds(x, y)) return;

    uint32_t targetColor = m_pixels[pixelIndex(x, y)];
    if (targetColor == newColor) return;

    std::queue<glm::ivec2> queue;
    queue.push({x, y});

    while (!queue.empty()) {
        glm::ivec2 p = queue.front();
        queue.pop();

        if (!inBounds(p.x, p.y)) continue;

        int idx = pixelIndex(p.x, p.y);
        if (m_pixels[idx] != targetColor) continue;

        recordPixelChange(idx, m_pixels[idx]);
        m_pixels[idx] = newColor;

        queue.push({p.x + 1, p.y});
        queue.push({p.x - 1, p.y});
        queue.push({p.x, p.y + 1});
        queue.push({p.x, p.y - 1});
    }

    m_canvasDirty = true;
    m_modified = true;
}

uint32_t PaintEditor::pickColor(int x, int y) {
    if (!inBounds(x, y)) return m_primaryColor;
    return m_pixels[pixelIndex(x, y)];
}

void PaintEditor::drawLine(int x0, int y0, int x1, int y1, uint32_t color) {
    // Bresenham's line algorithm
    int dx = std::abs(x1 - x0);
    int dy = std::abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (true) {
        paintPixel(x0, y0, color);

        if (x0 == x1 && y0 == y1) break;

        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void PaintEditor::drawRectangle(int x0, int y0, int x1, int y1, uint32_t color, bool filled) {
    int minX = std::min(x0, x1);
    int maxX = std::max(x0, x1);
    int minY = std::min(y0, y1);
    int maxY = std::max(y0, y1);

    if (filled) {
        for (int y = minY; y <= maxY; y++) {
            for (int x = minX; x <= maxX; x++) {
                paintPixel(x, y, color);
            }
        }
    } else {
        // Top and bottom
        for (int x = minX; x <= maxX; x++) {
            paintPixel(x, minY, color);
            paintPixel(x, maxY, color);
        }
        // Left and right
        for (int y = minY + 1; y < maxY; y++) {
            paintPixel(minX, y, color);
            paintPixel(maxX, y, color);
        }
    }
}

void PaintEditor::drawCircle(int cx, int cy, int radius, uint32_t color, bool filled) {
    // Midpoint circle algorithm
    if (filled) {
        for (int y = -radius; y <= radius; y++) {
            for (int x = -radius; x <= radius; x++) {
                if (x * x + y * y <= radius * radius) {
                    paintPixel(cx + x, cy + y, color);
                }
            }
        }
    } else {
        int x = radius;
        int y = 0;
        int err = 0;

        while (x >= y) {
            paintPixel(cx + x, cy + y, color);
            paintPixel(cx + y, cy + x, color);
            paintPixel(cx - y, cy + x, color);
            paintPixel(cx - x, cy + y, color);
            paintPixel(cx - x, cy - y, color);
            paintPixel(cx - y, cy - x, color);
            paintPixel(cx + y, cy - x, color);
            paintPixel(cx + x, cy - y, color);

            y++;
            err += 1 + 2 * y;
            if (2 * (err - x) + 1 > 0) {
                x--;
                err += 1 - 2 * x;
            }
        }
    }
}

void PaintEditor::beginStroke() {
    m_currentStroke = PaintCommand();
    m_strokeInProgress = true;
}

void PaintEditor::recordPixelChange(int index, uint32_t oldColor) {
    if (m_strokeInProgress) {
        m_currentStroke.changes.push_back({index, oldColor});
    }
}

void PaintEditor::endStroke() {
    if (m_strokeInProgress && !m_currentStroke.changes.empty()) {
        m_undoStack.push(m_currentStroke);
        // Clear redo stack on new action
        while (!m_redoStack.empty()) m_redoStack.pop();
        // Limit undo history
        while (m_undoStack.size() > MAX_UNDO) {
            // Can't easily pop from bottom of stack, so we just accept memory growth
            break;
        }
    }
    m_strokeInProgress = false;
}

void PaintEditor::undo() {
    if (m_undoStack.empty()) return;

    PaintCommand cmd = m_undoStack.top();
    m_undoStack.pop();

    // Create redo command with current colors
    PaintCommand redoCmd;
    for (auto& change : cmd.changes) {
        redoCmd.changes.push_back({change.first, m_pixels[change.first]});
        m_pixels[change.first] = change.second;
    }
    m_redoStack.push(redoCmd);
    m_canvasDirty = true;
    m_modified = true;
}

void PaintEditor::redo() {
    if (m_redoStack.empty()) return;

    PaintCommand cmd = m_redoStack.top();
    m_redoStack.pop();

    // Create undo command with current colors
    PaintCommand undoCmd;
    for (auto& change : cmd.changes) {
        undoCmd.changes.push_back({change.first, m_pixels[change.first]});
        m_pixels[change.first] = change.second;
    }
    m_undoStack.push(undoCmd);
    m_canvasDirty = true;
    m_modified = true;
}

bool PaintEditor::saveToFile(const std::string& path) {
    // Convert ABGR to RGBA for stb_image_write
    std::vector<uint8_t> rgbaData(CANVAS_WIDTH * CANVAS_HEIGHT * 4);
    for (int i = 0; i < CANVAS_WIDTH * CANVAS_HEIGHT; i++) {
        uint32_t pixel = m_pixels[i];
        rgbaData[i * 4 + 0] = pixel & 0xFF;           // R
        rgbaData[i * 4 + 1] = (pixel >> 8) & 0xFF;    // G
        rgbaData[i * 4 + 2] = (pixel >> 16) & 0xFF;   // B
        rgbaData[i * 4 + 3] = (pixel >> 24) & 0xFF;   // A
    }

    int result = stbi_write_png(path.c_str(), CANVAS_WIDTH, CANVAS_HEIGHT, 4, rgbaData.data(), CANVAS_WIDTH * 4);
    if (result) {
        m_currentFilePath = path;
        m_modified = false;
        return true;
    }
    return false;
}

bool PaintEditor::loadFromFile(const std::string& path) {
    int width, height, channels;
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);

    if (!data) return false;

    // Clear undo/redo
    while (!m_undoStack.empty()) m_undoStack.pop();
    while (!m_redoStack.empty()) m_redoStack.pop();

    // Resize or crop to 64x64
    for (int y = 0; y < CANVAS_HEIGHT; y++) {
        for (int x = 0; x < CANVAS_WIDTH; x++) {
            if (x < width && y < height) {
                int srcIdx = (y * width + x) * 4;
                uint8_t r = data[srcIdx + 0];
                uint8_t g = data[srcIdx + 1];
                uint8_t b = data[srcIdx + 2];
                uint8_t a = data[srcIdx + 3];
                m_pixels[pixelIndex(x, y)] = (a << 24) | (b << 16) | (g << 8) | r;
            } else {
                m_pixels[pixelIndex(x, y)] = 0xFFFFFFFF;  // White for out of bounds
            }
        }
    }

    stbi_image_free(data);
    m_currentFilePath = path;
    m_modified = false;
    m_canvasDirty = true;
    return true;
}

void PaintEditor::newCanvas() {
    // Clear undo/redo
    while (!m_undoStack.empty()) m_undoStack.pop();
    while (!m_redoStack.empty()) m_redoStack.pop();

    // Fill with white
    std::fill(m_pixels.begin(), m_pixels.end(), 0xFFFFFFFF);

    m_currentFilePath.clear();
    m_modified = false;
    m_canvasDirty = true;
}

void PaintEditor::addRecentColor(uint32_t color) {
    // Remove if already in list
    auto it = std::find(m_recentColors.begin(), m_recentColors.end(), color);
    if (it != m_recentColors.end()) {
        m_recentColors.erase(it);
    }
    // Add to front
    m_recentColors.insert(m_recentColors.begin(), color);
    // Trim to max
    while (m_recentColors.size() > MAX_RECENT_COLORS) {
        m_recentColors.pop_back();
    }
}

glm::ivec2 PaintEditor::screenToCanvas(const ImVec2& screenPos, const ImVec2& canvasOrigin) {
    return glm::ivec2(
        (int)((screenPos.x - canvasOrigin.x) / m_zoom),
        (int)((screenPos.y - canvasOrigin.y) / m_zoom)
    );
}

uint32_t PaintEditor::colorToABGR(float r, float g, float b, float a) {
    uint8_t rb = (uint8_t)(r * 255.0f);
    uint8_t gb = (uint8_t)(g * 255.0f);
    uint8_t bb = (uint8_t)(b * 255.0f);
    uint8_t ab = (uint8_t)(a * 255.0f);
    return (ab << 24) | (bb << 16) | (gb << 8) | rb;
}

void PaintEditor::ABGRToColor(uint32_t abgr, float& r, float& g, float& b, float& a) {
    r = (abgr & 0xFF) / 255.0f;
    g = ((abgr >> 8) & 0xFF) / 255.0f;
    b = ((abgr >> 16) & 0xFF) / 255.0f;
    a = ((abgr >> 24) & 0xFF) / 255.0f;
}

void PaintEditor::processShortcuts() {
    if (!m_isOpen) return;

    ImGuiIO& io = ImGui::GetIO();

    // Tool shortcuts (only when not typing in text field)
    if (!io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_B)) m_currentTool = PaintTool::Brush;
        if (ImGui::IsKeyPressed(ImGuiKey_E)) m_currentTool = PaintTool::Eraser;
        if (ImGui::IsKeyPressed(ImGuiKey_F)) m_currentTool = PaintTool::Fill;
        if (ImGui::IsKeyPressed(ImGuiKey_P)) m_currentTool = PaintTool::ColorPicker;
        if (ImGui::IsKeyPressed(ImGuiKey_L)) m_currentTool = PaintTool::Line;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) m_currentTool = PaintTool::Rectangle;
        if (ImGui::IsKeyPressed(ImGuiKey_C)) m_currentTool = PaintTool::Circle;
        if (ImGui::IsKeyPressed(ImGuiKey_G)) m_showGrid = !m_showGrid;
        if (ImGui::IsKeyPressed(ImGuiKey_M)) m_mirrorX = !m_mirrorX;
        if (ImGui::IsKeyPressed(ImGuiKey_X)) std::swap(m_primaryColor, m_secondaryColor);
        if (ImGui::IsKeyPressed(ImGuiKey_Slash) && io.KeyShift) m_showShortcuts = !m_showShortcuts;  // ?
    }

    // Ctrl shortcuts
    if (io.KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z)) undo();
        if (ImGui::IsKeyPressed(ImGuiKey_Y)) redo();
        if (ImGui::IsKeyPressed(ImGuiKey_S)) {
            if (!m_currentFilePath.empty()) {
                saveToFile(m_currentFilePath);
            } else {
                m_fileBrowserMode = FileBrowser::Mode::SAVE;
                m_fileBrowser.open(FileBrowser::Mode::SAVE, "Save Image", {".png"}, "assets/paint");
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_O)) {
            m_fileBrowserMode = FileBrowser::Mode::OPEN;
            m_fileBrowser.open(FileBrowser::Mode::OPEN, "Open Image", {".png"}, "assets/paint");
        }
        if (ImGui::IsKeyPressed(ImGuiKey_N)) {
            newCanvas();
        }
    }
}
