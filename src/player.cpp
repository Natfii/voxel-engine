#include "player.h"
#include "world.h"
#include "block_system.h"
#include "debug_state.h"
#include "logger.h"
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>

Player::Player(glm::vec3 position, glm::vec3 up, float yaw, float pitch)
    : Position(position), WorldUp(up), Yaw(yaw), Pitch(pitch),
      MovementSpeed(5.0f), MouseSensitivity(0.1f), FirstMouse(true),
      Velocity(0.0f), OnGround(false), InLiquid(false), NKeyPressed(false),
      NoclipMode(false), IsSprinting(false), SprintKeyPressed(false)
{
    updateVectors();
}

void Player::update(GLFWwindow* window, float deltaTime, World* world, bool processInput) {
    // Handle N key to toggle noclip mode (only if processing input)
    if (processInput && glfwGetKey(window, GLFW_KEY_N) == GLFW_PRESS) {
        if (!NKeyPressed) {
            NoclipMode = !NoclipMode;
            NKeyPressed = true;
            Logger::info() << "Noclip mode: " << (NoclipMode ? "ON" : "OFF");
            if (!NoclipMode) {
                // Reset velocity when entering physics mode
                Velocity = glm::vec3(0.0f);
            }
        }
    } else {
        NKeyPressed = false;
    }

    // Update mouse look (only if processing input)
    if (processInput && glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED) {
        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);

        if (FirstMouse) {
            LastX = float(xpos);
            LastY = float(ypos);
            FirstMouse = false;
        }

        float xoffset = float(xpos - LastX);
        float yoffset = float(LastY - ypos); // Reversed for Vulkan's flipped Y projection
        LastX = float(xpos);
        LastY = float(ypos);

        xoffset *= MouseSensitivity;
        yoffset *= MouseSensitivity;

        Yaw   += xoffset;
        Pitch += yoffset;

        if (Pitch > 89.0f)
            Pitch = 89.0f;
        if (Pitch < -89.0f)
            Pitch = -89.0f;

        updateVectors();
    }

    // Update movement based on mode
    if (NoclipMode) {
        // Noclip mode only works with input
        if (processInput) {
            updateNoclip(window, deltaTime);
        }
    } else {
        // Physics mode: always apply physics, but only process input if allowed
        updatePhysics(window, deltaTime, world, processInput);
    }
}

void Player::updateNoclip(GLFWwindow* window, float deltaTime) {
    // Original noclip movement (fly mode)
    float velocity = MovementSpeed * deltaTime;

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        Position += Front * velocity;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        Position -= Front * velocity;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        Position -= Right * velocity;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        Position += Right * velocity;
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
        Position += WorldUp * velocity;
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        Position -= WorldUp * velocity;
}

void Player::updatePhysics(GLFWwindow* window, float deltaTime, World* world, bool processInput) {
    // Handle sprint input
    // NOTE: Sprint toggle mode could be added in the future by:
    // - Adding a sprint_toggle_mode config option in config.ini
    // - Tracking sprint toggle state in SprintKeyPressed member
    // - Toggling IsSprinting on key press instead of holding
    // For now, using hold-to-sprint (Minecraft default behavior)
    bool sprintKeyDown = processInput && glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
    IsSprinting = sprintKeyDown && OnGround;  // Can only sprint on ground

    // WASD input for horizontal movement (only if processing input)
    glm::vec3 wishDir(0.0f);

    if (processInput) {
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            wishDir += glm::normalize(glm::vec3(Front.x, 0.0f, Front.z));
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            wishDir -= glm::normalize(glm::vec3(Front.x, 0.0f, Front.z));
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            wishDir -= Right;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            wishDir += Right;

        // Normalize diagonal movement
        if (glm::length(wishDir) > 0.0f) {
            wishDir = glm::normalize(wishDir);
        }
    }

    // Check if player is in liquid (check center of player)
    glm::vec3 checkPos = Position;
    int blockID = world->getBlockAt(checkPos.x, checkPos.y, checkPos.z);

    // Check if the block is a liquid using BlockRegistry
    // NOTE: Currently no liquid blocks are defined in assets/blocks/
    // When liquid blocks are added (water, lava), set their "isLiquid: true" in YAML
    InLiquid = false;
    if (blockID > 0) {
        try {
            const auto& blockDef = BlockRegistry::instance().get(blockID);
            InLiquid = blockDef.isLiquid;
        } catch (...) {
            // Invalid block ID, treat as air
            InLiquid = false;
        }
    }

    // Apply movement speed with sprint multiplier
    float moveSpeed = InLiquid ? SWIM_SPEED : WALK_SPEED;
    if (IsSprinting && !InLiquid) {
        moveSpeed *= SPRINT_MULTIPLIER;
    }
    glm::vec3 horizontalVel = wishDir * moveSpeed;

    // Jumping (only if processing input)
    if (processInput && glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
        if (InLiquid) {
            // Swim up in liquid
            Velocity.y = SWIM_SPEED * 0.8f;
        } else if (OnGround) {
            // Jump if on ground
            Velocity.y = JUMP_VELOCITY;
        }
    }

    // Apply gravity (only when not on ground or in liquid)
    // This prevents slow sinking when standing still
    if (!InLiquid && !OnGround) {
        Velocity.y -= GRAVITY * deltaTime;
    } else if (InLiquid) {
        // In liquid, apply gentle downward drift (only if not processing input, or no keys pressed)
        if (!processInput || (glfwGetKey(window, GLFW_KEY_SPACE) != GLFW_PRESS &&
            glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) != GLFW_PRESS)) {
            Velocity.y = -SWIM_SPEED * 0.3f;
        }
        // Swim down with shift (only if processing input)
        if (processInput && glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
            Velocity.y = -SWIM_SPEED * 0.8f;
        }
    }

    // Terminal velocity
    if (Velocity.y < -40.0f) Velocity.y = -40.0f;

    // *** Check ground state BEFORE movement ***
    // This ensures OnGround represents the state at the START of the frame
    // so jumping works correctly even when moving off edges
    OnGround = false;

    // Check if standing on ground - check 4 bottom corners of AABB
    // This is the standard approach in voxel games like Minecraft
    glm::vec3 feetPos = Position - glm::vec3(0.0f, PLAYER_EYE_HEIGHT, 0.0f);
    float halfWidth = PLAYER_WIDTH / 2.0f;

    // Check slightly below feet to detect ground
    bool groundDetected = false;
    float checkDistance = 0.05f;  // Check 0.05 units below feet

    // First verify player isn't inside a block (center check at feet level)
    int blockAtCenter = world->getBlockAt(feetPos.x, feetPos.y, feetPos.z);

    if (blockAtCenter == 0) {  // Only check for ground if not inside a block
        // Check center first as fallback for edge cases
        int centerBelow = world->getBlockAt(feetPos.x, feetPos.y - checkDistance, feetPos.z);
        if (centerBelow > 0) groundDetected = true;

        // Also check 4 corners - if ANY corner has solid block below, player is grounded
        // This allows jumping while walking off ledges (as long as one corner is still over ground)
        if (!groundDetected) {
            // Check back-left corner
            int bl = world->getBlockAt(feetPos.x - halfWidth, feetPos.y - checkDistance, feetPos.z - halfWidth);
            if (bl > 0) groundDetected = true;
        }

        if (!groundDetected) {
            // Check back-right corner
            int br = world->getBlockAt(feetPos.x + halfWidth, feetPos.y - checkDistance, feetPos.z - halfWidth);
            if (br > 0) groundDetected = true;
        }

        if (!groundDetected) {
            // Check front-left corner
            int fl = world->getBlockAt(feetPos.x - halfWidth, feetPos.y - checkDistance, feetPos.z + halfWidth);
            if (fl > 0) groundDetected = true;
        }

        if (!groundDetected) {
            // Check front-right corner
            int fr = world->getBlockAt(feetPos.x + halfWidth, feetPos.y - checkDistance, feetPos.z + halfWidth);
            if (fr > 0) groundDetected = true;
        }
    }

    if (groundDetected) {
        OnGround = true;
        // Don't stop velocity here - let collision resolution handle it
        // This prevents teleporting/floating when detecting ground early
    }

    // Now calculate movement and apply physics
    // Calculate total movement
    glm::vec3 movement = horizontalVel * deltaTime;
    movement.y = Velocity.y * deltaTime;

    // Resolve collisions
    resolveCollisions(movement, world);

    // Apply final movement
    Position += movement;

    // After movement, if we detected ground and are now on it, stop falling
    if (OnGround && Velocity.y < 0.0f) {
        Velocity.y = 0.0f;
    }
}

void Player::resolveCollisions(glm::vec3& movement, World* world) {
    // AABB collision resolution using axis-by-axis approach
    // Position is at eye level, feet are PLAYER_EYE_HEIGHT below

    // Resolve Y axis FIRST (like Minecraft)
    // This prevents edge clipping by settling vertical position before horizontal movement
    // Axis resolution order: Y → X → Z

    // DEBUG: Print position and movement (only if debug_collision is enabled)
    static int debugCounter = 0;
    bool shouldDebug = DebugState::instance().debugCollision.getValue();
    if (shouldDebug && debugCounter++ % 60 == 0 && std::abs(Velocity.y) > 0.1f) {
        glm::vec3 feetPos = Position - glm::vec3(0.0f, PLAYER_EYE_HEIGHT, 0.0f);
        std::cout << "DEBUG: Player pos=" << Position.y << " feet=" << feetPos.y
                  << " movement.y=" << movement.y << " velocity.y=" << Velocity.y << std::endl;
    }

    // CRITICAL FIX: If player is ALREADY stuck inside a block, push them out
    // But only if they're SIGNIFICANTLY stuck (not just barely touching)
    const float STUCK_THRESHOLD = 0.02f; // Only unstick if > 0.02 units inside

    if (checkCollision(Position, world)) {
        // Player is currently inside a block - calculate correction
        glm::vec3 feetPos = Position - glm::vec3(0.0f, PLAYER_EYE_HEIGHT, 0.0f);
        float blockGridY = std::ceil(feetPos.y / 0.5f) * 0.5f;
        float correctionY = (blockGridY + PLAYER_EYE_HEIGHT) - Position.y;

        // Only apply correction if it's significant (player is really stuck, not just touching edge)
        if (std::abs(correctionY) > STUCK_THRESHOLD) {
            Position.y += correctionY;
            Velocity.y = 0.0f; // Stop vertical velocity when unsticking

            if (shouldDebug && debugCounter % 60 == 0) {
                Logger::debug() << "Player stuck in block! Pushing up by " << correctionY;
            }
        }
    }

    // ===== Test Y axis FIRST (vertical movement) =====
    glm::vec3 testPos = Position + glm::vec3(0.0f, movement.y, 0.0f);
    bool yCollision = checkCollision(testPos, world);

    if (shouldDebug && debugCounter % 60 == 0 && std::abs(Velocity.y) > 0.1f) {
        Logger::debug() << "Y collision check at testPos.y=" << testPos.y << " result=" << yCollision;
    }

    if (yCollision) {
        // Collision detected!
        if (movement.y < 0.0f) {
            // Falling downward - snap feet to top of block grid
            glm::vec3 feetPos = Position - glm::vec3(0.0f, PLAYER_EYE_HEIGHT, 0.0f);
            // Round feet position UP to next 0.5 block boundary
            float blockGridY = std::ceil(feetPos.y / 0.5f) * 0.5f;
            // Set movement to place feet exactly on block grid
            movement.y = (blockGridY + PLAYER_EYE_HEIGHT) - Position.y;
        } else {
            // Moving upward (hitting ceiling) - just stop
            movement.y = 0.0f;
        }
        Velocity.y = 0.0f;
    }

    // ===== Test X axis (horizontal movement) =====
    testPos = Position + glm::vec3(movement.x, 0.0f, 0.0f);

    // Use horizontal collision check (from step height up) to allow ledge walking
    if (checkHorizontalCollision(testPos, world)) {
        // Collision detected - stop horizontal movement
        movement.x = 0.0f;
        Velocity.x = 0.0f;
    }

    // ===== Test Z axis (horizontal movement) =====
    testPos = Position + glm::vec3(0.0f, 0.0f, movement.z);

    // Use horizontal collision check (from step height up) to allow ledge walking
    if (checkHorizontalCollision(testPos, world)) {
        // Collision detected - stop horizontal movement
        movement.z = 0.0f;
        Velocity.z = 0.0f;
    }
}

bool Player::checkCollision(const glm::vec3& position, World* world) {
    // Player's AABB bounds (position is at eye level)
    glm::vec3 feetPos = position - glm::vec3(0.0f, PLAYER_EYE_HEIGHT, 0.0f);
    float halfWidth = PLAYER_WIDTH / 2.0f;

    // Check all blocks the player AABB could be touching
    // Player is 0.9 units tall (1.8 blocks), 0.25 units wide (0.5 blocks)
    glm::vec3 minBound = feetPos - glm::vec3(halfWidth, 0.0f, halfWidth);
    glm::vec3 maxBound = feetPos + glm::vec3(halfWidth, PLAYER_HEIGHT, halfWidth);

    // Convert to block coordinates (blocks are 0.5 units)
    int minX = (int)std::floor(minBound.x / 0.5f);
    int minY = (int)std::floor(minBound.y / 0.5f);
    int minZ = (int)std::floor(minBound.z / 0.5f);
    int maxX = (int)std::floor(maxBound.x / 0.5f);
    int maxY = (int)std::floor(maxBound.y / 0.5f);
    int maxZ = (int)std::floor(maxBound.z / 0.5f);

    // DEBUG: Print what we're checking (only if debug_collision is enabled)
    static int checkCounter = 0;
    bool shouldDebugCollision = DebugState::instance().debugCollision.getValue() && (checkCounter++ % 120 == 0);
    if (shouldDebugCollision) {
        std::cout << "DEBUG checkCollision: feet=" << feetPos.y
                  << " minY=" << minY << " maxY=" << maxY
                  << " (blocks " << minY << " to " << maxY << ")" << std::endl;
    }

    // Check each block in the range
    for (int x = minX; x <= maxX; x++) {
        for (int y = minY; y <= maxY; y++) {
            for (int z = minZ; z <= maxZ; z++) {
                // Convert block coordinates back to world position
                float worldX = x * 0.5f;
                float worldY = y * 0.5f;
                float worldZ = z * 0.5f;

                int blockID = world->getBlockAt(worldX, worldY, worldZ);

                if (shouldDebugCollision && y == minY) {
                    std::cout << "  Checking block (" << x << "," << y << "," << z
                              << ") world(" << worldX << "," << worldY << "," << worldZ
                              << ") = " << blockID << std::endl;
                }

                if (blockID > 0) {  // Solid block (not air)
                    if (shouldDebugCollision) {
                        Logger::debug() << "  COLLISION at block (" << x << "," << y << "," << z << ")!";
                    }
                    return true;  // Collision detected
                }
            }
        }
    }

    return false;  // No collision
}

bool Player::checkHorizontalCollision(const glm::vec3& position, World* world) {
    // Check collision from knee height up to head (allows walking off ledges)
    glm::vec3 feetPos = position - glm::vec3(0.0f, PLAYER_EYE_HEIGHT, 0.0f);
    float halfWidth = PLAYER_WIDTH / 2.0f;

    // Start checking from 0.3 units (0.6 blocks) above feet - this is knee/step height
    const float stepHeight = 0.3f;
    glm::vec3 minBound = feetPos + glm::vec3(-halfWidth, stepHeight, -halfWidth);
    glm::vec3 maxBound = feetPos + glm::vec3(halfWidth, PLAYER_HEIGHT, halfWidth);

    // Convert to block coordinates (blocks are 0.5 units)
    int minX = (int)std::floor(minBound.x / 0.5f);
    int minY = (int)std::floor(minBound.y / 0.5f);
    int minZ = (int)std::floor(minBound.z / 0.5f);
    int maxX = (int)std::floor(maxBound.x / 0.5f);
    int maxY = (int)std::floor(maxBound.y / 0.5f);
    int maxZ = (int)std::floor(maxBound.z / 0.5f);

    // Check each block in the range
    for (int x = minX; x <= maxX; x++) {
        for (int y = minY; y <= maxY; y++) {
            for (int z = minZ; z <= maxZ; z++) {
                // Convert block coordinates back to world position
                float worldX = x * 0.5f;
                float worldY = y * 0.5f;
                float worldZ = z * 0.5f;

                int blockID = world->getBlockAt(worldX, worldY, worldZ);
                if (blockID > 0) {  // Solid block (not air)
                    return true;  // Collision detected
                }
            }
        }
    }

    return false;  // No collision
}

glm::mat4 Player::getViewMatrix() const {
    return glm::lookAt(Position, Position + Front, Up);
}

void Player::updateVectors() {
    glm::vec3 front;
    front.x = cos(glm::radians(Yaw)) * cos(glm::radians(Pitch));
    front.y = sin(glm::radians(Pitch));
    front.z = sin(glm::radians(Yaw)) * cos(glm::radians(Pitch));
    Front = glm::normalize(front);

    Right = glm::normalize(glm::cross(Front, WorldUp));
    Up    = glm::normalize(glm::cross(Right, Front));
}

void Player::resetMouse() {
    FirstMouse = true;
}
