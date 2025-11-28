/**
 * @file particle_editor.h
 * @brief 2D Particle effect editor
 */

#pragma once

#include "particle/particle_effect.h"
#include "particle/particle_emitter.h"
#include "editor/file_browser.h"
#include <string>
#include <vector>
#include <memory>

class VulkanRenderer;

/**
 * @brief 2D Particle effect editor
 *
 * Provides UI for creating and editing particle effects.
 * Launched via the "particaleditor" console command.
 */
class ParticleEditor {
public:
    ParticleEditor();
    ~ParticleEditor();

    /**
     * @brief Initialize the editor
     */
    bool initialize(VulkanRenderer* renderer);

    /**
     * @brief Update the editor
     */
    void update(float deltaTime);

    /**
     * @brief Render the editor UI
     */
    void render();

    /**
     * @brief Check if editor is open
     */
    bool isOpen() const { return m_isOpen; }

    /**
     * @brief Open the editor
     */
    void open(const std::string& effectPath = "");

    /**
     * @brief Close the editor
     */
    void close();

private:
    // UI panels
    void renderMenuBar();
    void renderToolbar();
    void renderEmitterList();
    void renderEmitterProperties();
    void renderViewport();
    void renderTimeline();

    // File operations
    void newEffect();
    void saveEffect();
    void loadEffect();

    // Presets
    void applyPreset(const std::string& presetName);

    // State
    bool m_isOpen = false;
    VulkanRenderer* m_renderer = nullptr;

    // Effect data
    ParticleEffect m_effect;
    std::vector<std::unique_ptr<ParticleEmitter>> m_emitters;
    int m_selectedEmitter = 0;

    // Playback
    bool m_isPlaying = true;
    float m_time = 0.0f;
    float m_playbackSpeed = 1.0f;

    // Viewport
    float m_zoom = 1.0f;
    glm::vec2 m_viewOffset = glm::vec2(0.0f);
    glm::vec4 m_backgroundColor = glm::vec4(0.1f, 0.1f, 0.15f, 1.0f);

    // File paths
    std::string m_currentPath;
    FileBrowser m_fileBrowser;
    enum class BrowserMode { NONE, SAVE, LOAD };
    BrowserMode m_browserMode = BrowserMode::NONE;
};
