/**
 * @file skeletal_editor.h
 * @brief Main 3D skeletal annotation editor
 */

#pragma once

#include "editor/editor_camera.h"
#include "editor/skeleton_editor_state.h"
#include "editor/file_browser.h"
#include "mesh/mesh_loader.h"
#include <memory>
#include <string>

class VulkanRenderer;

/**
 * @brief Main skeletal annotation editor
 *
 * Provides UI for loading models, placing bones, and saving rigs.
 * Launched via the "3deditor" console command.
 */
class SkeletalEditor {
public:
    SkeletalEditor();
    ~SkeletalEditor();

    /**
     * @brief Initialize the editor
     * @param renderer Pointer to main renderer (for mesh rendering)
     * @return True if initialization succeeded
     */
    bool initialize(VulkanRenderer* renderer);

    /**
     * @brief Update the editor (call each frame)
     * @param deltaTime Frame delta time
     */
    void update(float deltaTime);

    /**
     * @brief Render the editor UI and viewport
     */
    void render();

    /**
     * @brief Check if editor is open
     */
    bool isOpen() const { return m_isOpen; }

    /**
     * @brief Open the editor with optional model path
     */
    void open(const std::string& modelPath = "");

    /**
     * @brief Close the editor
     */
    void close();

    /**
     * @brief Process input for the editor
     */
    void processInput(float mouseX, float mouseY, bool leftDown, bool rightDown, float scrollDelta);

private:
    // UI panels
    void renderMenuBar();
    void renderToolbar();
    void renderHierarchyPanel();
    void renderPropertiesPanel();
    void renderWizardPanel();
    void renderViewport();
    void renderStatusBar();

    // Gizmo handling
    void renderGizmo();

    // File operations
    void loadModel(const std::string& path);
    void saveRig();
    void loadRig();
    void newRig();

    // State
    bool m_isOpen = false;
    VulkanRenderer* m_renderer = nullptr;

    // Components
    EditorCamera m_camera;
    SkeletonEditorState m_state;
    GLTFScene m_scene;
    bool m_hasModel = false;

    // UI state
    bool m_showWizard = true;
    bool m_showHierarchy = true;
    bool m_showProperties = true;
    int m_gizmoOperation = 0;  // 0=translate

    // Input state
    float m_lastMouseX = 0.0f;
    float m_lastMouseY = 0.0f;
    bool m_isDraggingCamera = false;

    // File dialog state
    std::string m_currentModelPath;
    std::string m_currentRigPath;
    FileBrowser m_fileBrowser;
    enum class BrowserMode { NONE, SAVE_RIG, LOAD_RIG, LOAD_MODEL };
    BrowserMode m_browserMode = BrowserMode::NONE;

    // Viewport
    float m_viewportWidth = 800.0f;
    float m_viewportHeight = 600.0f;
};
