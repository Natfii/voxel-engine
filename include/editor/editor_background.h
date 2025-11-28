/**
 * @file editor_background.h
 * @brief Cute animated procedural 2D background for editor-only mode
 *
 * Renders a charming top-down pixel art world simulation with:
 * - Procedural terrain (grass, mountains, snow, lakes, beaches)
 * - Animated trees swaying in the wind
 * - Little walking people (black dots) that wander around
 */

#pragma once

#include <vector>
#include <glm/glm.hpp>
#include <cstdint>

/**
 * @brief A little person that wanders around the map
 */
struct LittlePerson {
    glm::vec2 position;
    glm::vec2 target;
    float speed;
    float waitTime = 0.0f;
    bool waiting = false;
};

/**
 * @brief A tree that sways in the wind
 */
struct Tree {
    glm::vec2 position;
    float swayOffset;  // Random offset for wind animation
    int type;          // 0 = pine, 1 = oak
};

/**
 * @brief A boat bobbing on the water
 */
struct Boat {
    glm::vec2 position;
    float angle;       // Direction boat is facing
    float bobOffset;   // Random offset for bobbing animation
};

/**
 * @brief A town where people gather
 */
struct Town {
    float x, y;
    int size;          // Number of buildings
};

/**
 * @brief Animated procedural 2D background for editor mode
 */
class EditorBackground {
public:
    EditorBackground();
    ~EditorBackground() = default;

    /**
     * @brief Initialize the background with given dimensions
     * @param width Canvas width in pixels
     * @param height Canvas height in pixels
     */
    void initialize(int width, int height);

    /**
     * @brief Update animation state
     * @param deltaTime Time since last update
     */
    void update(float deltaTime);

    /**
     * @brief Render the background to ImGui
     */
    void render();

    /**
     * @brief Regenerate the world with a new seed
     */
    void regenerate();

private:
    // Terrain generation
    void generateTerrain();
    float noise2D(float x, float y, int octaves = 4);
    uint32_t getBiomeColor(float elevation, float moisture);

    // Entity management
    void spawnPeople(int count);
    void spawnTrees();
    void spawnTowns();
    void spawnBoats();
    void updatePeople(float deltaTime);

    // Canvas data
    int m_width = 0;
    int m_height = 0;
    std::vector<uint32_t> m_pixels;       // Base terrain

    // Terrain data
    std::vector<float> m_elevation;
    std::vector<float> m_moisture;

    // Entities
    std::vector<LittlePerson> m_people;
    std::vector<Tree> m_trees;
    std::vector<Boat> m_boats;
    std::vector<Town> m_towns;

    // Animation state
    float m_time = 0.0f;
    float m_windTime = 0.0f;
    int m_seed = 12345;
    bool m_paused = false;  // Pause checkbox state

    // ImGui texture
    unsigned int m_textureID = 0;
    bool m_needsUpload = true;
};
