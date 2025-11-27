/**
 * @file player.h
 * @brief First-person player controller with physics-based movement
 *
 */

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <GLFW/glfw3.h>
#include <string>

// Forward declaration
class World;

/**
 * @brief First-person player controller with realistic physics simulation
 *
 * The Player class implements a Minecraft-style first-person character controller with:
 * - Gravity-based physics with collision detection
 * - WASD movement with sprint capability
 * - Mouse-look camera control
 * - Jumping and ground detection
 * - Optional noclip mode for flying through terrain
 *
 * Physics Features:
 * - Gravity: 16.0 blocks/s² (scaled for 0.5 unit blocks)
 * - Jump height: 1.25 blocks (like Minecraft)
 * - Walk speed: 4.3 blocks/s
 * - Sprint multiplier: 1.5x walk speed
 * - AABB collision detection with world terrain
 *
 * Coordinate System:
 * - Position represents eye position (camera origin)
 * - Player height: 1.8 blocks (0.9 world units)
 * - Player width: 0.5 blocks (0.25 world units)
 * - Eye height: 1.62 blocks above feet
 *
 * @note The class uses public members for easy access from rendering code.
 *       This follows a data-oriented design pattern common in game engines.
 */
class Player {
public:
    /**
     * @brief Constructs a player at the specified position and orientation
     *
     * @param position Initial eye position in world space
     * @param up World up vector (typically (0, 1, 0))
     * @param yaw Initial horizontal rotation in degrees
     * @param pitch Initial vertical rotation in degrees
     */
    Player(glm::vec3 position, glm::vec3 up, float yaw, float pitch);

    /**
     * @brief Resets mouse tracking to prevent camera jump
     *
     * Call this when re-capturing the mouse cursor or after teleporting
     * to prevent sudden camera movements.
     */
    void resetMouse();

    /**
     * @brief Updates player physics, movement, and camera
     *
     * Handles input processing, physics integration, collision detection,
     * and camera vector updates. Should be called once per frame.
     *
     * @param window GLFW window for input polling
     * @param deltaTime Time elapsed since last frame (seconds)
     * @param world World instance for collision detection
     * @param processInput If false, disables input processing (for pause menu)
     */
    void update(GLFWwindow* window, float deltaTime, World* world, bool processInput = true);

    /**
     * @brief Gets the view matrix for rendering
     *
     * Constructs a view matrix using the current position, front, and up vectors.
     * In third-person mode, camera is positioned behind and above the player.
     *
     * @return View matrix for use in MVP transformation
     */
    glm::mat4 getViewMatrix() const;

    /**
     * @brief Gets the player's body position (for model rendering)
     *
     * Returns the position where the player model should be rendered,
     * which is at feet level (eye position minus eye height).
     *
     * @return Position at player's feet for model rendering
     */
    glm::vec3 getBodyPosition() const;

    /**
     * @brief Checks if in third-person view mode
     * @return True if third-person mode is active
     */
    bool isThirdPerson() const { return ThirdPersonMode; }

    /**
     * @brief Gets the camera position (eye position in 1st person, offset in 3rd person)
     * @return Camera world position for rendering
     */
    glm::vec3 getCameraPosition() const;

    /**
     * @brief Checks if the player is currently in liquid (swimming)
     *
     * @return True if player is submerged in liquid (water, lava, etc.)
     */
    bool isSwimming() const { return m_inLiquid; }

    /**
     * @brief Checks if the camera/head is specifically underwater
     *
     * @return True if the camera position (eye level) is in liquid
     */
    bool isCameraUnderwater() const { return m_cameraUnderwater; }

    /**
     * @brief Saves player state to disk
     *
     * Saves position, rotation, velocity, and other state to player.dat file
     *
     * @param worldPath Path to world directory
     * @return True if save succeeded, false on error
     */
    bool savePlayerState(const std::string& worldPath) const;

    /**
     * @brief Loads player state from disk
     *
     * Loads position, rotation, velocity, and other state from player.dat file
     *
     * @param worldPath Path to world directory
     * @return True if load succeeded, false if file doesn't exist
     */
    bool loadPlayerState(const std::string& worldPath);

    // ========== Public Camera State ==========
    // Public for easy access from rendering and gameplay code

    glm::vec3 Position;    ///< Eye position in world space
    glm::vec3 Front;       ///< Forward direction vector (normalized)
    glm::vec3 Up;          ///< Up direction vector (normalized)
    glm::vec3 Right;       ///< Right direction vector (normalized)
    glm::vec3 WorldUp;     ///< World up vector (typically (0, 1, 0))

    float Yaw;             ///< Horizontal rotation in degrees
    float Pitch;           ///< Vertical rotation in degrees (clamped to ±89°)
    float MovementSpeed;   ///< Base movement speed (world units/second)
    float MouseSensitivity;///< Mouse sensitivity multiplier

    bool NoclipMode;       ///< If true, disables physics and allows free flight
    bool ThirdPersonMode;  ///< If true, camera is behind player
    float ThirdPersonDistance; ///< Distance behind player in third-person mode

private:
    // ========== Mouse Tracking ==========
    bool m_firstMouse;       ///< True until first mouse movement (prevents camera jump)
    float m_lastX;           ///< Last mouse X position
    float m_lastY;           ///< Last mouse Y position

    // ========== Physics State ==========
    glm::vec3 m_velocity;    ///< Current velocity vector (world units/second)
    bool m_onGround;         ///< True if player is standing on a solid block
    bool m_inLiquid;         ///< True if player is submerged in liquid (reserved for future)
    bool m_cameraUnderwater; ///< True if camera/head is specifically in liquid (for fog)
    float m_submergence;     ///< How deep in liquid (0.0 = not submerged, 1.0 = fully submerged)
    bool m_nKeyPressed;      ///< Tracks N key state for noclip toggle
    bool m_f3KeyPressed;     ///< Tracks F3 key state for third-person toggle
    bool m_isSprinting;      ///< True if currently sprinting
    bool m_sprintKeyPressed; ///< Tracks sprint key state for toggle mode (reserved)

    // ========== Player Dimensions ==========
    // All dimensions in world units (blocks are 1.0 world units)
    static constexpr float PLAYER_WIDTH = 0.5f;       ///< Player width (0.5 blocks wide, tighter than Minecraft)
    static constexpr float PLAYER_HEIGHT = 1.8f;      ///< Player height (1.8 blocks tall, like Minecraft)
    static constexpr float PLAYER_EYE_HEIGHT = 1.6f;  ///< Eye height from feet (1.6 blocks)

    // ========== Physics Constants ==========
    static constexpr float GRAVITY = 32.0f;            ///< Gravity acceleration (32 blocks/s²)
    static constexpr float JUMP_VELOCITY = 8.4f;       ///< Initial jump velocity (~1.1 block jump height)
    static constexpr float WALK_SPEED = 4.3f;          ///< Base walk speed (4.3 blocks/s)
    static constexpr float SPRINT_MULTIPLIER = 1.5f;   ///< Sprint speed multiplier (6.45 blocks/s when sprinting)
    static constexpr float SWIM_SPEED = 3.0f;          ///< Swimming speed (3.0 blocks/s)
    static constexpr float BUOYANCY_FORCE = 15.5f;     ///< Upward force when in water (almost neutral buoyancy)
    static constexpr float WATER_DRAG = 5.0f;          ///< Water drag coefficient (higher = more drag, damping)

    // ========== Private Methods ==========

    /**
     * @brief Updates camera direction vectors from yaw and pitch
     *
     * Recalculates Front, Right, and Up vectors after rotation changes.
     * Called internally after mouse movement.
     */
    void updateVectors();

    /**
     * @brief Updates physics simulation with gravity and collision
     *
     * Handles:
     * - Ground detection
     * - Gravity application
     * - Jump input
     * - Sprint state
     * - Collision resolution
     *
     * @param window GLFW window for input
     * @param deltaTime Time step for integration
     * @param world World for collision queries
     * @param processInput If false, ignores input
     */
    void updatePhysics(GLFWwindow* window, float deltaTime, World* world, bool processInput = true);

    /**
     * @brief Updates noclip (free flight) mode
     *
     * Allows flying in all directions with WASD + Space/Shift.
     * No collision detection in this mode.
     *
     * @param window GLFW window for input
     * @param deltaTime Time step for movement
     */
    void updateNoclip(GLFWwindow* window, float deltaTime);

    /**
     * @brief Checks if the player collides with terrain at the given position
     *
     * Tests if the player's AABB intersects any solid blocks.
     *
     * @param position Position to test (eye position)
     * @param world World to query for blocks
     * @return True if collision detected, false otherwise
     */
    bool checkCollision(const glm::vec3& position, World* world);

    /**
     * @brief Checks only if player's feet are stuck in a solid block
     *
     * Used by unstuck mechanism to avoid triggering on head/ceiling collisions.
     * Only checks the bottom 0.5 blocks of the player hitbox.
     *
     * @param position Position to test (eye level)
     * @param world World to query
     * @return True if feet are stuck in solid block
     */
    bool checkFeetCollision(const glm::vec3& position, World* world);

    /**
     * @brief Checks horizontal (XZ plane) collision only
     *
     * Used for horizontal movement resolution without affecting vertical motion.
     *
     * @param position Position to test
     * @param world World to query
     * @return True if horizontal collision detected
     */
    bool checkHorizontalCollision(const glm::vec3& position, World* world);

    /**
     * @brief Resolves collisions by modifying the movement vector
     *
     * Implements sliding collision response - allows player to slide along
     * walls while maintaining movement in non-colliding directions.
     *
     * @param movement Movement vector to modify (in/out parameter)
     * @param world World to query for collision
     */
    void resolveCollisions(glm::vec3& movement, World* world);

    /**
     * @brief Checks if player is standing on solid ground at given position
     *
     * Checks center and 4 corners of player's feet AABB for solid blocks below.
     * Liquid blocks are not considered ground.
     *
     * @param position Eye position to check from
     * @param world World to query for blocks
     * @return True if any solid block is detected below feet
     */
    bool checkGroundAtPosition(const glm::vec3& position, World* world);
};