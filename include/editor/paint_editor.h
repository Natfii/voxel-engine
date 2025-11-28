/**
 * @file paint_editor.h
 * @brief 2D Pixel Paint Editor (64x64 canvas)
 *
 * A lightweight pixel art editor for creating sprites and textures.
 * Opened via console command: painteditor
 */

#pragma once

#include <imgui.h>
#include <glm/glm.hpp>
#include <vector>
#include <stack>
#include <string>
#include <queue>
#include <functional>
#include "editor/file_browser.h"

/**
 * @brief Available painting tools
 */
enum class PaintTool {
    Brush,
    Eraser,
    Fill,
    ColorPicker,
    Line,
    Rectangle,
    Circle
};

/**
 * @brief Undo/redo command for pixel operations
 */
struct PaintCommand {
    std::vector<std::pair<int, uint32_t>> changes;  // index -> old color
};

/**
 * @brief 2D Pixel Paint Editor
 *
 * Features:
 * - 64x64 RGBA canvas
 * - Brush, eraser, fill, color picker, line, rectangle, circle tools
 * - Undo/redo with 256 action limit
 * - Zoom (1x-16x) and pan
 * - Grid overlay toggle
 * - Mirror drawing (X/Y axis)
 * - Color palette with recent colors
 * - Save/load PNG files
 */
class PaintEditor {
public:
    PaintEditor();
    ~PaintEditor();

    /**
     * @brief Render the paint editor window
     */
    void render();

    /**
     * @brief Check if editor is open
     */
    bool isOpen() const { return m_isOpen; }

    /**
     * @brief Open the editor
     */
    void open() { m_isOpen = true; }

    /**
     * @brief Close the editor
     */
    void close() { m_isOpen = false; }

private:
    // ========== Canvas State ==========
    static constexpr int CANVAS_WIDTH = 64;
    static constexpr int CANVAS_HEIGHT = 64;
    std::vector<uint32_t> m_pixels;         // RGBA8 pixel buffer
    bool m_canvasDirty = true;              // Need texture upload

    // ========== Tool State ==========
    PaintTool m_currentTool = PaintTool::Brush;
    int m_brushSize = 1;                    // 1-8 pixels
    uint32_t m_primaryColor = 0x000000FF;   // Black (ABGR)
    uint32_t m_secondaryColor = 0xFFFFFFFF; // White (ABGR)

    // ========== View State ==========
    float m_zoom = 8.0f;                    // 8x default zoom
    glm::vec2 m_panOffset = {0, 0};         // Canvas pan offset
    bool m_showGrid = true;                 // Grid overlay
    bool m_mirrorX = false;                 // Mirror drawing X-axis
    bool m_mirrorY = false;                 // Mirror drawing Y-axis

    // ========== Undo/Redo ==========
    std::stack<PaintCommand> m_undoStack;
    std::stack<PaintCommand> m_redoStack;
    PaintCommand m_currentStroke;           // Current stroke being recorded
    bool m_strokeInProgress = false;
    static constexpr int MAX_UNDO = 256;

    // ========== Palette ==========
    std::vector<uint32_t> m_recentColors;
    static constexpr int MAX_RECENT_COLORS = 16;
    std::vector<uint32_t> m_presetColors;

    // ========== Line/Shape Drawing ==========
    bool m_drawingShape = false;
    glm::ivec2 m_shapeStart = {0, 0};
    glm::ivec2 m_shapeEnd = {0, 0};

    // ========== File I/O ==========
    std::string m_currentFilePath;
    bool m_modified = false;
    FileBrowser m_fileBrowser;
    FileBrowser::Mode m_fileBrowserMode = FileBrowser::Mode::OPEN;  // Track browser mode

    // ========== UI State ==========
    bool m_isOpen = false;
    bool m_showShortcuts = false;

    // ========== Rendering ==========
    void renderToolbar();
    void renderPalette();
    void renderColorBar();
    void renderCanvas();
    void renderStatusBar();
    void renderShortcutsOverlay();

    // ========== Tool Operations ==========
    void paintPixel(int x, int y, uint32_t color);
    void paintBrush(int x, int y, uint32_t color);
    void erasePixel(int x, int y);
    void floodFill(int x, int y, uint32_t newColor);
    uint32_t pickColor(int x, int y);
    void drawLine(int x0, int y0, int x1, int y1, uint32_t color);
    void drawRectangle(int x0, int y0, int x1, int y1, uint32_t color, bool filled);
    void drawCircle(int cx, int cy, int radius, uint32_t color, bool filled);

    // ========== Undo/Redo ==========
    void beginStroke();
    void recordPixelChange(int index, uint32_t oldColor);
    void endStroke();
    void undo();
    void redo();

    // ========== File I/O ==========
    bool saveToFile(const std::string& path);
    bool loadFromFile(const std::string& path);
    void newCanvas();

    // ========== Helpers ==========
    int pixelIndex(int x, int y) const { return y * CANVAS_WIDTH + x; }
    bool inBounds(int x, int y) const { return x >= 0 && x < CANVAS_WIDTH && y >= 0 && y < CANVAS_HEIGHT; }
    void addRecentColor(uint32_t color);
    glm::ivec2 screenToCanvas(const ImVec2& screenPos, const ImVec2& canvasOrigin);
    uint32_t colorToABGR(float r, float g, float b, float a);
    void ABGRToColor(uint32_t abgr, float& r, float& g, float& b, float& a);

    // ========== Input ==========
    void processShortcuts();
};
