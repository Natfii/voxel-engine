#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <GLFW/glfw3.h>

// Forward declaration
class World;

class Player {
public:
    Player(glm::vec3 position, glm::vec3 up, float yaw, float pitch);

    void resetMouse();
    void update(GLFWwindow* window, float deltaTime, World* world, bool processInput = true);
    glm::mat4 getViewMatrix() const;

    // Public for easy access from rendering
    glm::vec3 Position;
    glm::vec3 Front;
    glm::vec3 Up;
    glm::vec3 Right;
    glm::vec3 WorldUp;

    float Yaw;
    float Pitch;
    float MovementSpeed;
    float MouseSensitivity;

    // Physics mode toggle
    bool NoclipMode;

private:
    // Mouse tracking
    bool FirstMouse;
    float LastX;
    float LastY;

    // Physics properties
    glm::vec3 Velocity;      // Current velocity (x, y, z)
    bool OnGround;           // True if player is standing on a block
    bool InLiquid;           // True if player is in liquid
    bool NKeyPressed;        // Track N key state for toggle
    bool IsSprinting;        // True if player is currently sprinting
    bool SprintKeyPressed;   // Track sprint key state for toggle

    // Player dimensions (in world units, blocks are 0.5 units)
    static constexpr float PLAYER_WIDTH = 0.25f;  // 0.5 blocks wide (tighter than Minecraft for better control)
    static constexpr float PLAYER_HEIGHT = 0.9f;  // 1.8 blocks tall (like Minecraft)
    static constexpr float PLAYER_EYE_HEIGHT = 0.8f; // Eye height from feet (1.62 blocks in Minecraft)

    // Physics constants
    static constexpr float GRAVITY = 16.0f;       // Blocks/s^2 (32 * 0.5 scale)
    static constexpr float JUMP_VELOCITY = 4.2f;  // Initial jump velocity (allows 1.25 block jump)
    static constexpr float WALK_SPEED = 2.15f;    // Walking speed in world units/s (4.3 blocks/s)
    static constexpr float SPRINT_MULTIPLIER = 1.5f;  // Sprint speed multiplier
    static constexpr float SWIM_SPEED = 1.5f;     // Swimming speed

    void updateVectors();
    void updatePhysics(GLFWwindow* window, float deltaTime, World* world, bool processInput = true);
    void updateNoclip(GLFWwindow* window, float deltaTime);
    bool checkCollision(const glm::vec3& position, World* world);
    bool checkHorizontalCollision(const glm::vec3& position, World* world);
    void resolveCollisions(glm::vec3& movement, World* world);
};