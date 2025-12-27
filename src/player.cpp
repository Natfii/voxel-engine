/**
 * @file player.cpp
 * @brief First-person player controller with AABB physics and collision detection
 *
 */

#include "player.h"
#include "world.h"
#include "block_system.h"
#include "debug_state.h"
#include "logger.h"
#include "terrain_constants.h"
#include "key_bindings.h"
#include "animation/skeleton_animator.h"
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>

Player::Player(glm::vec3 position, glm::vec3 up, float yaw, float pitch)
    : Position(position), WorldUp(up), Yaw(yaw), Pitch(pitch),
      MovementSpeed(5.0f), MouseSensitivity(0.1f), m_firstMouse(true),
      m_velocity(0.0f), m_onGround(false), m_inLiquid(false), m_cameraUnderwater(false),
      m_submergence(0.0f), m_nKeyPressed(false), m_f3KeyPressed(false), NoclipMode(false),
      ThirdPersonMode(false), ThirdPersonDistance(7.5f), m_isSprinting(false),
      m_sprintKeyPressed(false), m_animator(nullptr), m_useModelPhysics(false),
      m_bodyYaw(yaw), m_bodyYawVelocity(0.0f), m_jumpPressedLastFrame(false)
{
    updateVectors();

    // Initialize tongue grapple system
    m_tongueGrapple = std::make_unique<PlayerPhysics::TongueGrapple>();
    PlayerPhysics::TongueGrappleConfig tongueConfig;
    tongueConfig.tongueSpeed = 200.0f;    // Very fast tongue shot
    tongueConfig.maxRange = 50.0f;        // 50 blocks max range
    tongueConfig.cooldownTime = 0.0f;     // No cooldown - can instantly re-shoot
    tongueConfig.ropeSpring = 10.0f;      // Springy rope
    tongueConfig.ropeDamping = 0.7f;      // Bouncy but controlled
    tongueConfig.releaseBoost = 5.0f;     // Nice upward boost on release
    tongueConfig.maxSwingSpeed = 40.0f;   // Fast swinging
    m_tongueGrapple->initialize(nullptr, tongueConfig);  // No skeleton needed yet
}

void Player::initializeModelPhysics(SkeletonAnimator* animator) {
    m_animator = animator;

    if (!animator || !animator->hasSkeletonLoaded()) {
        Logger::warning() << "Player::initializeModelPhysics: No skeleton loaded";
        m_useModelPhysics = false;
        return;
    }

    // Create and initialize model physics
    m_modelPhysics = std::make_unique<PlayerPhysics::PlayerModelPhysics>();

    PlayerPhysics::PlayerModelPhysicsConfig config;
    config.enableBoneCollision = true;
    config.enableSquish = true;
    config.enableHeadTracking = true;

    // Tune squish parameters for exaggerated "cartoon squishy" feel
    config.squishParams.springStiffness = 15.0f;   // Lower = slower recovery (more visible)
    config.squishParams.dampingRatio = 0.3f;       // Lower = more bouncy
    config.squishParams.maxCompression = 0.4f;     // More compression allowed (was 0.65)
    config.squishParams.maxExpansion = 1.8f;       // More expansion for volume preservation
    config.squishParams.influenceRadius = 1.5f;    // Larger radius for more bones affected
    config.squishParams.impactMultiplier = 0.5f;   // Higher = more squish per impact
    config.squishParams.volumePreservation = 0.8f; // Higher = bulge more when compressed
    config.squishParams.recoverySpeed = 2.0f;      // Slower recovery = more visible

    // Head tracking parameters
    config.headTrackingParams.maxYawAngle = 70.0f;
    config.headTrackingParams.maxPitchAngle = 45.0f;
    config.headTrackingParams.trackingSpeed = 10.0f;
    config.headTrackingParams.enableNeckBlend = true;
    config.headTrackingParams.neckBlendRatio = 0.25f;

    m_modelPhysics->initialize(animator->getSkeleton(), config);
    m_useModelPhysics = true;

    Logger::info() << "Player model physics initialized (bone collision, squish, head tracking)";
}

void Player::update(GLFWwindow* window, float deltaTime, World* world, bool processInput) {
    const auto& keys = KeyBindings::instance();

    // Handle noclip toggle key (only if processing input)
    if (processInput && glfwGetKey(window, keys.noclip) == GLFW_PRESS) {
        if (!m_nKeyPressed) {
            NoclipMode = !NoclipMode;
            m_nKeyPressed = true;
            Logger::info() << "Noclip mode: " << (NoclipMode ? "ON" : "OFF");
            if (NoclipMode) {
                // Reset tongue grapple when entering noclip
                if (m_tongueGrapple) {
                    m_tongueGrapple->reset();
                }
            } else {
                // Reset velocity when entering physics mode
                m_velocity = glm::vec3(0.0f);
            }
        }
    } else {
        m_nKeyPressed = false;
    }

    // Handle third-person toggle key (only if processing input)
    if (processInput && glfwGetKey(window, keys.thirdPerson) == GLFW_PRESS) {
        if (!m_f3KeyPressed) {
            ThirdPersonMode = !ThirdPersonMode;
            m_f3KeyPressed = true;
            Logger::info() << "Third-person mode: " << (ThirdPersonMode ? "ON" : "OFF");
        }
    } else {
        m_f3KeyPressed = false;
    }

    // Update mouse look (only if processing input)
    if (processInput && glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED) {
        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);

        if (m_firstMouse) {
            m_lastX = float(xpos);
            m_lastY = float(ypos);
            m_firstMouse = false;
        }

        float xoffset = float(xpos - m_lastX);
        float yoffset = float(m_lastY - ypos); // Reversed for Vulkan's flipped Y projection
        m_lastX = float(xpos);
        m_lastY = float(ypos);

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

    // Update body yaw lag (inflatable costume effect - body lags behind head)
    updateBodyYawLag(deltaTime);

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

    // Update model physics (head tracking, squish deformation)
    if (m_useModelPhysics && m_modelPhysics && m_animator) {
        // Calculate body forward direction from body yaw (not camera yaw!)
        // Body lags behind camera for inflatable costume effect
        float bodyYawRad = glm::radians(m_bodyYaw);
        glm::vec3 bodyForward = glm::normalize(glm::vec3(
            std::cos(bodyYawRad),
            0.0f,
            std::sin(bodyYawRad)
        ));

        // Update model physics with current state
        // Pass camera Front for head tracking, but body forward for body orientation
        m_modelPhysics->update(deltaTime, world, Position, m_velocity, Front, bodyForward);

        // Update bone transforms from animator
        if (m_animator->hasSkeletonLoaded()) {
            const auto& transforms = m_animator->getAllFinalTransforms();
            glm::mat4 modelTransform = glm::translate(glm::mat4(1.0f), getBodyPosition());
            // Apply BODY yaw rotation to model (not camera yaw!)
            // Body rotates independently with spring physics lag
            modelTransform = glm::rotate(modelTransform, glm::radians(m_bodyYaw + 90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            m_modelPhysics->updateBoneTransforms(transforms, modelTransform);
        }

        // NOTE: applyToAnimator is now called from main.cpp AFTER playerAnimator.update()
        // to prevent animation from overwriting squish scales
    }
}

void Player::updateNoclip(GLFWwindow* window, float deltaTime) {
    const auto& keys = KeyBindings::instance();

    // Noclip movement (fly mode) - 2x normal speed for faster exploration
    float velocity = MovementSpeed * 2.0f * deltaTime;

    if (glfwGetKey(window, keys.moveForward) == GLFW_PRESS)
        Position += Front * velocity;
    if (glfwGetKey(window, keys.moveBackward) == GLFW_PRESS)
        Position -= Front * velocity;
    if (glfwGetKey(window, keys.moveLeft) == GLFW_PRESS)
        Position -= Right * velocity;
    if (glfwGetKey(window, keys.moveRight) == GLFW_PRESS)
        Position += Right * velocity;
    if (glfwGetKey(window, keys.jump) == GLFW_PRESS)
        Position += WorldUp * velocity;
    if (glfwGetKey(window, keys.sprint) == GLFW_PRESS)
        Position -= WorldUp * velocity;
}

/**
 * @brief Physics-based player movement with gravity, collision, and liquid mechanics
 *
 * Physics System Overview:
 * ========================
 *
 * This function implements a semi-implicit Euler integrator for player physics,
 * similar to Minecraft's physics system. The key features are:
 *
 * 1. **Movement Modes**:
 *    - Ground movement: WALK_SPEED (2.15 world units/sec = 4.3 blocks/sec)
 *    - Sprint: WALK_SPEED * 1.5 (3.225 world units/sec = 6.45 blocks/sec)
 *    - Swimming: SWIM_SPEED (1.5 world units/sec = 3.0 blocks/sec) with vertical control
 *    - Jumping: Instant upward velocity (JUMP_VELOCITY = 4.2 world units/sec)
 *
 * 2. **Gravity Integration** (Semi-implicit Euler):
 *    - m_velocity updated first: v(t+dt) = v(t) - g*dt
 *    - Position updated with new velocity: p(t+dt) = p(t) + v(t+dt)*dt
 *    - This prevents energy gain on slopes (unlike explicit Euler)
 *    - Gravity: 16.0 world units/sec² (32 blocks/sec²)
 *    - Terminal velocity: -40.0 world units/sec (-80 blocks/sec)
 *
 * 3. **Ground Detection** (4-corner AABB check):
 *    - Checks center + 4 foot corners for solid blocks below
 *    - Check distance: 0.05 units below feet
 *    - Allows edge-of-block jumping (coyote time effect)
 *    - m_onGround set BEFORE movement to enable jump at frame start
 *
 * 4. **Collision Resolution** (Axis-by-axis AABB):
 *    - Y-axis resolved first (prevents edge clipping)
 *    - Then X and Z axes independently
 *    - Uses resolveCollisions() with swept AABB tests
 *    - See resolveCollisions() documentation for algorithm details
 *
 * 5. **Swimming Mechanics**:
 *    - Liquid detection via BlockRegistry.isLiquid flag
 *    - Space key: swim up (0.8 * SWIM_SPEED upward velocity)
 *    - Shift key: swim down (0.8 * SWIM_SPEED downward velocity)
 *    - No gravity in liquid, gentle drift down when idle
 *
 * Physics Constants (from player.h):
 * -----------------------------------
 * - GRAVITY: 16.0 world units/sec² (16 blocks/sec²)
 * - JUMP_VELOCITY: 4.2 world units/sec (reaches ~0.55 blocks high)
 * - WALK_SPEED: 2.15 world units/sec (2.15 blocks/sec)
 * - SPRINT_MULTIPLIER: 1.5x (3.225 blocks/sec when sprinting)
 * - SWIM_SPEED: 1.5 world units/sec (1.5 blocks/sec)
 * - PLAYER_HEIGHT: 0.9 world units (0.9 blocks, reduced from Minecraft's 1.8)
 * - PLAYER_WIDTH: 0.25 world units (0.25 blocks)
 * - PLAYER_EYE_HEIGHT: 0.8 world units (0.8 blocks, Position is at eye level)
 *
 * Physics Math Example:
 * ---------------------
 * Jump height: h = v₀² / (2g) = 4.2² / (2*16) = 0.55125 world units ≈ 0.55 blocks
 * Jump duration: t = 2v₀ / g = 2*4.2 / 16 = 0.525 seconds
 * Sprint speed: 2.15 * 1.5 = 3.225 world units/sec = 3.225 blocks/sec
 *
 * Note: Blocks are 1.0 world units in size
 *
 * @param window GLFW window for input
 * @param deltaTime Frame time in seconds (typically ~0.016 for 60 FPS)
 * @param world World instance for collision queries
 * @param processInput Whether to process keyboard input (false during menu)
 */
void Player::updatePhysics(GLFWwindow* window, float deltaTime, World* world, bool processInput) {
    const auto& keys = KeyBindings::instance();

    // Handle sprint input
    // NOTE: Sprint toggle mode could be added in the future by:
    // - Adding a sprint_toggle_mode config option in config.ini
    // - Tracking sprint toggle state in m_sprintKeyPressed member
    // - Toggling m_isSprinting on key press instead of holding
    // For now, using hold-to-sprint (Minecraft default behavior)
    bool sprintKeyDown = processInput && glfwGetKey(window, keys.sprint) == GLFW_PRESS;
    m_isSprinting = sprintKeyDown && m_onGround;  // Can only sprint on ground

    // WASD input for horizontal movement (only if processing input)
    glm::vec3 wishDir(0.0f);

    if (processInput) {
        if (glfwGetKey(window, keys.moveForward) == GLFW_PRESS)
            wishDir += glm::normalize(glm::vec3(Front.x, 0.0f, Front.z));
        if (glfwGetKey(window, keys.moveBackward) == GLFW_PRESS)
            wishDir -= glm::normalize(glm::vec3(Front.x, 0.0f, Front.z));
        if (glfwGetKey(window, keys.moveLeft) == GLFW_PRESS)
            wishDir -= Right;
        if (glfwGetKey(window, keys.moveRight) == GLFW_PRESS)
            wishDir += Right;

        // Normalize diagonal movement
        if (glm::length(wishDir) > 0.0f) {
            wishDir = glm::normalize(wishDir);
        }
    }

    // Calculate submergence level by checking multiple points along player height
    // Position is at eye level, feet are below
    glm::vec3 playerFeet = Position - glm::vec3(0.0f, PLAYER_EYE_HEIGHT, 0.0f);
    glm::vec3 headPos = playerFeet + glm::vec3(0.0f, PLAYER_HEIGHT, 0.0f);

    // Check if camera (eye position) is specifically in liquid for fog effect
    int cameraBlockID = world->getBlockAt(Position.x, Position.y, Position.z);
    m_cameraUnderwater = false;
    if (cameraBlockID > 0) {
        try {
            const auto& blockDef = BlockRegistry::instance().get(cameraBlockID);
            m_cameraUnderwater = blockDef.isLiquid;
        } catch (...) {
            m_cameraUnderwater = false;
        }
    }

    // Sample 5 points from feet to head to calculate submergence
    int liquidCount = 0;
    const int samplePoints = 5;
    for (int i = 0; i < samplePoints; ++i) {
        float t = static_cast<float>(i) / (samplePoints - 1);
        glm::vec3 samplePos = playerFeet + glm::vec3(0.0f, PLAYER_HEIGHT * t, 0.0f);
        int blockID = world->getBlockAt(samplePos.x, samplePos.y, samplePos.z);

        if (blockID > 0) {
            try {
                const auto& blockDef = BlockRegistry::instance().get(blockID);
                if (blockDef.isLiquid) {
                    liquidCount++;
                }
            } catch (...) {
                // Invalid block ID, treat as air
            }
        }
    }

    // Calculate submergence (0.0 = not in water, 1.0 = fully submerged)
    m_submergence = static_cast<float>(liquidCount) / static_cast<float>(samplePoints);
    m_inLiquid = (m_submergence > 0.0f);

    // Apply movement speed with sprint multiplier
    float moveSpeed = m_inLiquid ? SWIM_SPEED : WALK_SPEED;
    if (m_isSprinting && !m_inLiquid) {
        moveSpeed *= SPRINT_MULTIPLIER;
    }
    glm::vec3 horizontalVel = wishDir * moveSpeed;

    // Jumping (only if processing input)
    bool jumpPressed = processInput && glfwGetKey(window, keys.jump) == GLFW_PRESS;
    bool jumpPressEdge = jumpPressed && !m_jumpPressedLastFrame;  // Just pressed this frame
    m_jumpPressedLastFrame = jumpPressed;

    if (jumpPressed) {
        if (m_inLiquid) {
            // Jump strength scales with submergence
            // Less submerged = stronger jump (can exit water easier)
            float jumpStrength = JUMP_VELOCITY * (0.3f + 0.4f * (1.0f - m_submergence));
            m_velocity.y = jumpStrength;
        } else if (m_onGround) {
            // Jump if on ground
            m_velocity.y = JUMP_VELOCITY;
        }
    }

    // ========== TONGUE GRAPPLE CONTROLS ==========
    // Hold jump while in air to shoot and maintain tongue grapple
    // Release jump to detach - can instantly shoot again
    bool canUseTongue = !m_onGround && !m_inLiquid;
    if (m_tongueGrapple && canUseTongue) {
        if (jumpPressed) {
            // Holding jump in air - shoot tongue if not already active
            if (!m_tongueGrapple->isAttached() && !m_tongueGrapple->isShooting()) {
                m_tongueGrapple->shoot(Position, Front, world);
            }
        } else {
            // Released jump - detach tongue if attached
            if (m_tongueGrapple->isAttached()) {
                m_tongueGrapple->release(m_velocity);
            } else if (m_tongueGrapple->isShooting()) {
                // Cancel shooting tongue if released before it hits
                m_tongueGrapple->reset();
            }
        }
    } else if (m_tongueGrapple && !canUseTongue) {
        // On ground or in water - reset tongue state
        if (m_tongueGrapple->isAttached() || m_tongueGrapple->isShooting()) {
            m_tongueGrapple->release(m_velocity);
        }
    }

    // Reel in with left mouse button while swinging
    if (m_tongueGrapple && m_tongueGrapple->isAttached()) {
        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
            m_tongueGrapple->reelIn(deltaTime, Position);
        }
    }

    // Update tongue physics (swing forces, shooting progress, cooldown)
    if (m_tongueGrapple) {
        m_tongueGrapple->update(deltaTime, world, Position, m_velocity,
                                 Front, GRAVITY, m_onGround, m_inLiquid);
    }

    // Water physics overrides ground detection
    // If in liquid, treat as "not on ground" for physics purposes
    bool physicsOnGround = m_onGround && !m_inLiquid;

    // Apply gravity and buoyancy
    if (!physicsOnGround) {
        // Always apply gravity
        m_velocity.y -= GRAVITY * deltaTime;

        // Apply buoyancy force when in water (counters gravity)
        if (m_inLiquid) {
            m_velocity.y += BUOYANCY_FORCE * m_submergence * deltaTime;
        }
    }

    // Swim down with crouch key (only if processing input and in water)
    if (m_inLiquid && processInput && glfwGetKey(window, keys.crouch) == GLFW_PRESS) {
        m_velocity.y -= SWIM_SPEED * 1.5f * deltaTime;  // Extra downward force
    }

    // Terminal velocity
    using namespace PhysicsConstants;
    if (m_velocity.y < TERMINAL_VELOCITY) m_velocity.y = TERMINAL_VELOCITY;

    // *** Check ground state BEFORE movement ***
    // This ensures m_onGround represents the state at the START of the frame
    // so jumping works correctly even when moving off edges
    m_onGround = checkGroundAtPosition(Position, world);

    // Apply water drag to reduce velocity when in water
    if (m_inLiquid) {
        float drag = 1.0f - (WATER_DRAG * m_submergence * deltaTime);
        drag = glm::max(drag, 0.0f);  // Prevent negative drag
        horizontalVel *= drag;
        m_velocity.x *= drag;
        m_velocity.z *= drag;
        // Also apply some drag to vertical velocity
        m_velocity.y *= glm::max(1.0f - (WATER_DRAG * 0.5f * m_submergence * deltaTime), 0.0f);
    }

    // Now calculate movement and apply physics
    // Calculate total movement
    glm::vec3 movement = horizontalVel * deltaTime;
    movement.y = m_velocity.y * deltaTime;

    // Resolve collisions
    resolveCollisions(movement, world);

    // Apply final movement
    Position += movement;

    // Void boundary protection: Prevent falling through the world
    // With world_height=16, MIN_WORLD_Y is typically -256. Use -300 as safety threshold.
    const float VOID_BOUNDARY = -300.0f;
    if (Position.y < VOID_BOUNDARY) {
        Position.y = 100.0f;  // Teleport to safe Y coordinate
        m_velocity.y = 0.0f;  // Reset vertical velocity
        Logger::warning() << "Player fell through void, teleporting to safety";
    }

    // COLLISION FIX: Re-check ground state AFTER movement to prevent bobbing
    // The pre-movement ground check is for jump logic only
    // We need to check AFTER movement to know if collision resolution stopped us
    m_onGround = checkGroundAtPosition(Position, world);

    // After movement, stabilize velocity based on ACTUAL ground state (not pre-movement state)
    if (m_onGround) {
        if (!m_inLiquid) {
            // On land: completely zero vertical velocity to prevent bobbing
            m_velocity.y = 0.0f;
        } else {
            // In water on floor: only zero if velocity is very small
            if (m_velocity.y < 0.0f && std::abs(m_velocity.y) < 0.1f) {
                m_velocity.y = 0.0f;
            }
        }
    }
}

/**
 * @brief AABB collision resolution using axis-by-axis swept tests
 *
 * Collision Resolution Algorithm:
 * ================================
 *
 * This function resolves collisions between the player's AABB (Axis-Aligned Bounding Box)
 * and the voxel world using a swept collision approach tested per axis.
 *
 * Algorithm Overview:
 * -------------------
 * 1. **Unstick Phase**: If player is stuck inside a block, push them out
 * 2. **Y-Axis Test**: Test vertical movement first, snap to grid if collision
 * 3. **X-Axis Test**: Test horizontal X movement, stop if collision
 * 4. **Z-Axis Test**: Test horizontal Z movement, stop if collision
 *
 * Why Y-First Resolution Order?
 * ------------------------------
 * Resolving Y (vertical) before X/Z (horizontal) prevents "edge clipping" bugs:
 * - If player is falling and moving forward simultaneously
 * - Resolving X first might push player slightly into a wall
 * - Then resolving Y would snap player downward INTO the wall (stuck)
 * - By resolving Y first, player settles onto ground before horizontal movement
 * - This matches Minecraft's collision resolution order
 *
 * AABB Coordinate System:
 * -----------------------
 * - Player.Position = eye level (0.85 units above feet)
 * - feetPos = Position - vec3(0, PLAYER_EYE_HEIGHT, 0)
 * - AABB extends from feetPos to feetPos + vec3(0, PLAYER_HEIGHT, 0)
 * - AABB width: PLAYER_WIDTH (0.25 units) centered on feetPos.xz
 *
 * Grid Snapping on Collision:
 * ----------------------------
 * When falling downward and hitting ground:
 * - Feet position is snapped UP to nearest 0.5-unit boundary (block grid)
 * - Formula: blockGridY = ceil(feetPos.y / 0.5) * 0.5
 * - This ensures perfect alignment with block tops (prevents Z-fighting)
 * - Example: feetPos.y = 3.47 → snaps to 3.5 (top of block at y=3.0-3.5)
 *
 * Unstick Mechanism:
 * ------------------
 * If player is already inside a block (can happen due to rounding errors):
 * - Calculate correction to push player to next grid boundary
 * - Only apply if correction > STUCK_THRESHOLD (0.02 units)
 * - Prevents jittering from tiny floating-point errors
 * - Sets m_velocity.y = 0 to prevent continued falling
 *
 * Collision Detection:
 * --------------------
 * - Uses checkCollision() for full AABB tests (Y axis)
 * - Uses checkHorizontalCollision() for X/Z axes (allows ledge walking)
 * - checkHorizontalCollision starts from step height (0.3 units above feet)
 * - This allows player to walk off ledges smoothly without instant collision
 *
 * Example Scenario:
 * -----------------
 * Player falling at velocity (-2, -15, 3) with deltaTime = 0.016:
 * 1. Movement vector: (-0.032, -0.24, 0.048)
 * 2. Test Y: Position + (0, -0.24, 0) → collision detected below
 * 3. Snap feet to block grid: movement.y adjusted from -0.24 to -0.05
 * 4. Set m_velocity.y = 0 (stop falling)
 * 5. Test X: Position + (-0.032, 0, 0) → no collision, keep movement.x
 * 6. Test Z: Position + (0, 0, 0.048) → no collision, keep movement.z
 * 7. Final movement: (-0.032, -0.05, 0.048) → player lands smoothly
 *
 * @param movement [in/out] Movement vector for this frame, modified by collision
 * @param world World instance for collision queries
 */
void Player::resolveCollisions(glm::vec3& movement, World* world) {
    // AABB collision resolution using axis-by-axis approach
    // Position is at eye level, feet are PLAYER_EYE_HEIGHT below

    // ===== Model Physics: Bone Collision Detection =====
    // Check for collisions using per-bone capsules (for squish effect)
    // This runs in parallel with AABB collision - bone collision triggers squish,
    // while AABB collision handles the actual movement resolution
    if (m_useModelPhysics && m_modelPhysics) {
        PlayerPhysics::CollisionResult boneCollision;
        if (m_modelPhysics->getBoneCollision().checkCollision(world, boneCollision)) {
            // Bone collision detected - trigger squish deformation
            float impactForce = glm::length(m_velocity);
            float velocityDot = -glm::dot(glm::normalize(m_velocity + glm::vec3(0.0001f)),
                                           boneCollision.contactNormal);
            impactForce *= glm::max(velocityDot, 0.0f);

            // Lower threshold for more responsive squish (was 0.5f)
            if (impactForce > 0.1f) {
                // Amplify impact force for more visible effect
                m_modelPhysics->getSquishSystem().onCollision(
                    boneCollision.contactPoint,
                    boneCollision.contactNormal,
                    impactForce * 3.0f  // Amplify for visibility
                );
            }
        }
    }

    // Resolve Y axis FIRST (like Minecraft)
    // This prevents edge clipping by settling vertical position before horizontal movement
    // Axis resolution order: Y → X → Z

    // DEBUG: Print position and movement (only if debug_collision is enabled)
    static int debugCounter = 0;
    bool shouldDebug = DebugState::instance().debugCollision.getValue();
    if (shouldDebug && debugCounter++ % 60 == 0 && std::abs(m_velocity.y) > 0.1f) {
        glm::vec3 feetPos = Position - glm::vec3(0.0f, PLAYER_EYE_HEIGHT, 0.0f);
        Logger::debug() << "Player pos=" << Position.y << " feet=" << feetPos.y
                        << " movement.y=" << movement.y << " velocity.y=" << m_velocity.y;
    }

    // CRITICAL FIX: If player's FEET are stuck inside a block, push them out
    // BUG FIX (2025-11-26): Only check FEET collision, not full body
    // Previously, hitting a ceiling with your head would trigger this and push you UP
    // Now we only push up when feet are genuinely stuck (e.g., after chunk load)
    // OPTIMIZATION: Only check every 10 frames (unstuck is rare, this saves 90% of these checks)
    using namespace PhysicsConstants;
    static int unstuckCheckCounter = 0;
    if (++unstuckCheckCounter % 10 == 0) {
        // Use checkFeetCollision instead of checkCollision to avoid ceiling false positives
        if (checkFeetCollision(Position, world)) {
            // Player's feet are stuck inside a block - calculate correction
            glm::vec3 feetPos = Position - glm::vec3(0.0f, PLAYER_EYE_HEIGHT, 0.0f);
            float blockGridY = std::floor(feetPos.y) + 1.0f;  // Floor + 1 to place on top of block
            float correctionY = (blockGridY + PLAYER_EYE_HEIGHT) - Position.y;

            // Only apply correction if it's significant (player is really stuck, not just touching edge)
            if (std::abs(correctionY) > STUCK_THRESHOLD) {
                Position.y += correctionY;
                m_velocity.y = 0.0f; // Stop vertical velocity when unsticking

                if (shouldDebug && debugCounter % 60 == 0) {
                    Logger::debug() << "Player feet stuck in block! Pushing up by " << correctionY;
                }
            }
        }
    }

    // ===== Test Y axis FIRST (vertical movement) =====
    glm::vec3 testPos = Position + glm::vec3(0.0f, movement.y, 0.0f);
    bool yCollision = checkCollision(testPos, world);

    if (shouldDebug && debugCounter % 60 == 0 && std::abs(m_velocity.y) > 0.1f) {
        Logger::debug() << "Y collision check at testPos.y=" << testPos.y << " result=" << yCollision;
    }

    if (yCollision) {
        // Collision detected!
        if (movement.y < 0.0f) {
            // Falling downward
            if (!m_inLiquid) {
                // On land: snap feet to top of block grid for precise landing
                glm::vec3 feetPos = Position - glm::vec3(0.0f, PLAYER_EYE_HEIGHT, 0.0f);
                // Floor to get the block below, then add 1.0 to place on top
                float blockGridY = std::floor(feetPos.y) + 1.0f;
                float targetY = blockGridY + PLAYER_EYE_HEIGHT;

                // Only snap if there's meaningful correction needed (> 0.001 units)
                // This prevents micro-bouncing from floating point precision issues
                float correction = targetY - Position.y;
                if (std::abs(correction) > 0.001f) {
                    movement.y = correction;
                } else {
                    // Already perfectly aligned - just stop
                    movement.y = 0.0f;
                }
            } else {
                // In water: don't snap to grid, just stop movement to prevent bouncing
                movement.y = 0.0f;
            }
        } else {
            // Moving upward (hitting ceiling) - just stop
            movement.y = 0.0f;
        }

        // Don't zero velocity in water - let buoyancy/drag handle it
        if (!m_inLiquid) {
            m_velocity.y = 0.0f;
        }
    }

    // ===== HEAD CEILING CHECK =====
    // Before allowing horizontal movement, check if head is touching ceiling
    // This prevents walking under low ceilings and clipping head into blocks
    bool headTouchingCeiling = false;
    {
        glm::vec3 feetPos = Position - glm::vec3(0.0f, PLAYER_EYE_HEIGHT, 0.0f);
        float halfWidth = PLAYER_WIDTH / 2.0f;

        // Check the head level (top of player hitbox)
        float headY = feetPos.y + PLAYER_HEIGHT;
        int headBlockY = static_cast<int>(std::floor(headY));

        // Check blocks at head height in all 4 corners + center
        const glm::vec2 checkPoints[5] = {
            {0.0f, 0.0f},           // center
            {-halfWidth, -halfWidth}, // corners
            { halfWidth, -halfWidth},
            {-halfWidth,  halfWidth},
            { halfWidth,  halfWidth}
        };

        for (const auto& pt : checkPoints) {
            int blockID = world->getBlockAt(feetPos.x + pt.x, static_cast<float>(headBlockY), feetPos.z + pt.y);
            if (blockID > 0) {
                const auto& blockDef = BlockRegistry::instance().get(blockID);
                if (!blockDef.isLiquid) {
                    headTouchingCeiling = true;
                    break;
                }
            }
        }
    }

    // ===== Test X axis (horizontal movement) =====
    // OPTIMIZATION: Skip collision check if movement is negligible (saves ~33% of checks when standing still)
    if (std::abs(movement.x) > 0.001f) {
        testPos = Position + glm::vec3(movement.x, 0.0f, 0.0f);

        // Use horizontal collision check (from step height up) to allow ledge walking
        // Also block movement if head is touching ceiling (prevents clipping under low ceilings)
        if (checkHorizontalCollision(testPos, world) || headTouchingCeiling) {
            // Collision detected - stop horizontal movement
            movement.x = 0.0f;
            m_velocity.x = 0.0f;
        }
    }

    // ===== Test Z axis (horizontal movement) =====
    // OPTIMIZATION: Skip collision check if movement is negligible (saves ~33% of checks when standing still)
    if (std::abs(movement.z) > 0.001f) {
        testPos = Position + glm::vec3(0.0f, 0.0f, movement.z);

        // Use horizontal collision check (from step height up) to allow ledge walking
        // Also block movement if head is touching ceiling (prevents clipping under low ceilings)
        if (checkHorizontalCollision(testPos, world) || headTouchingCeiling) {
            // Collision detected - stop horizontal movement
            movement.z = 0.0f;
            m_velocity.z = 0.0f;
        }
    }

}

bool Player::checkGroundAtPosition(const glm::vec3& position, World* world) {
    using namespace PhysicsConstants;

    glm::vec3 feetPos = position - glm::vec3(0.0f, PLAYER_EYE_HEIGHT, 0.0f);
    float halfWidth = PLAYER_WIDTH / 2.0f;
    const float checkDistance = GROUND_CHECK_DISTANCE;

    // Check center
    int centerBelow = world->getBlockAt(feetPos.x, feetPos.y - checkDistance, feetPos.z);
    if (centerBelow > 0 && !BlockRegistry::instance().get(centerBelow).isLiquid) {
        return true;
    }

    // Check 4 corners
    const glm::vec2 corners[4] = {
        {-halfWidth, -halfWidth},  // back-left
        { halfWidth, -halfWidth},  // back-right
        {-halfWidth,  halfWidth},  // front-left
        { halfWidth,  halfWidth}   // front-right
    };

    for (const auto& corner : corners) {
        int block = world->getBlockAt(
            feetPos.x + corner.x,
            feetPos.y - checkDistance,
            feetPos.z + corner.y
        );
        if (block > 0 && !BlockRegistry::instance().get(block).isLiquid) {
            return true;
        }
    }

    return false;
}

/**
 * @brief Full AABB collision detection against the voxel world
 *
 * Collision Detection Algorithm:
 * ===============================
 *
 * This function checks if the player's full AABB (from feet to head) would
 * collide with any solid blocks in the world at the given position.
 *
 * Algorithm Steps:
 * ----------------
 * 1. Calculate player's AABB bounds in world space
 * 2. Convert AABB bounds to block coordinates (divide by 0.5)
 * 3. Iterate through all blocks within AABB range
 * 4. Return true if ANY block is solid (blockID > 0)
 *
 * AABB Bounds Calculation:
 * ------------------------
 * - Input position is at eye level (0.85 units above feet)
 * - feetPos = position - vec3(0, PLAYER_EYE_HEIGHT, 0)
 * - minBound = feetPos - vec3(halfWidth, 0, halfWidth)
 * - maxBound = feetPos + vec3(halfWidth, PLAYER_HEIGHT, halfWidth)
 * - This creates a box from feet to head, centered on player
 *
 * Block Coordinate Conversion:
 * ----------------------------
 * Blocks are 0.5 units in size, so:
 * - blockCoord = floor(worldCoord / 0.5)
 * - Example: worldPos = 3.7 → blockCoord = floor(7.4) = 7
 * - Block 7 spans world coordinates [3.5, 4.0)
 *
 * Optimization Note:
 * ------------------
 * This function checks ALL blocks in the AABB range, which is typically:
 * - Width: 1-2 blocks (player is 0.5 blocks wide)
 * - Height: 2-3 blocks (player is 1.8 blocks tall)
 * - Depth: 1-2 blocks
 * - Total: Usually 4-12 block checks per collision test
 *
 * Example:
 * --------
 * Player at position (5.0, 10.85, 5.0):
 * - feetPos = (5.0, 10.0, 5.0)
 * - minBound = (4.875, 10.0, 4.875)  [halfWidth = 0.125]
 * - maxBound = (5.125, 10.9, 5.125)  [height = 0.9]
 * - Block range X: floor(9.75) to floor(10.25) = 9 to 10
 * - Block range Y: floor(20.0) to floor(21.8) = 20 to 21
 * - Block range Z: floor(9.75) to floor(10.25) = 9 to 10
 * - Checks 2×2×2 = 8 blocks total
 *
 * @param position Position to test (at eye level)
 * @param world World instance for block queries
 * @return true if collision detected, false if clear
 */
bool Player::checkCollision(const glm::vec3& position, World* world) {
    // Player's AABB bounds (position is at eye level)
    glm::vec3 feetPos = position - glm::vec3(0.0f, PLAYER_EYE_HEIGHT, 0.0f);
    float halfWidth = PLAYER_WIDTH / 2.0f;

    // Check all blocks the player AABB could be touching
    // Player is 0.9 units tall (1.8 blocks), 0.25 units wide (0.5 blocks)
    glm::vec3 minBound = feetPos - glm::vec3(halfWidth, 0.0f, halfWidth);
    glm::vec3 maxBound = feetPos + glm::vec3(halfWidth, PLAYER_HEIGHT, halfWidth);

    // Convert to block coordinates (blocks are 1.0 units)
    int minX = (int)std::floor(minBound.x);
    int minY = (int)std::floor(minBound.y);
    int minZ = (int)std::floor(minBound.z);
    int maxX = (int)std::floor(maxBound.x);
    int maxY = (int)std::floor(maxBound.y);
    int maxZ = (int)std::floor(maxBound.z);

    // DEBUG: Print what we're checking (only if debug_collision is enabled)
    static int checkCounter = 0;
    bool shouldDebugCollision = DebugState::instance().debugCollision.getValue() && (checkCounter++ % 120 == 0);
    if (shouldDebugCollision) {
        Logger::debug() << "checkCollision: feet=" << feetPos.y
                        << " minY=" << minY << " maxY=" << maxY
                        << " (blocks " << minY << " to " << maxY << ")";
    }

    // Check each block in the range
    for (int x = minX; x <= maxX; x++) {
        for (int y = minY; y <= maxY; y++) {
            for (int z = minZ; z <= maxZ; z++) {
                // Convert block coordinates back to world position
                float worldX = static_cast<float>(x);
                float worldY = static_cast<float>(y);
                float worldZ = static_cast<float>(z);

                int blockID = world->getBlockAt(worldX, worldY, worldZ);

                if (shouldDebugCollision && y == minY) {
                    Logger::debug() << "  Checking block (" << x << "," << y << "," << z
                                    << ") world(" << worldX << "," << worldY << "," << worldZ
                                    << ") = " << blockID;
                }

                // Check if block is solid (not air and not liquid)
                if (blockID > 0) {
                    const auto& blockDef = BlockRegistry::instance().get(blockID);
                    if (!blockDef.isLiquid) {  // Only collide with solid blocks, not liquids
                        if (shouldDebugCollision) {
                            Logger::debug() << "  COLLISION at block (" << x << "," << y << "," << z << ")!";
                        }
                        return true;  // Collision detected
                    }
                }
            }
        }
    }

    return false;  // No collision
}

/**
 * @brief Feet-only collision detection for unstuck mechanism
 *
 * This function checks ONLY if the player's feet are stuck inside a solid block.
 * Unlike checkCollision() which checks the entire body, this only checks the
 * bottom 0.5 blocks of the player's hitbox.
 *
 * Why Separate Feet Check?
 * ------------------------
 * The unstuck mechanism should only trigger when feet are stuck in terrain
 * (e.g., after chunk loading, teleporting, or terrain updates). It should NOT
 * trigger when the player's head hits a ceiling - that's a normal collision
 * handled by the Y-axis collision resolution (which stops upward movement).
 *
 * BUG FIX (2025-11-26): Previously, bumping your head on a low ceiling would
 * trigger the unstuck mechanism (because checkCollision returns true for head
 * collision), which would incorrectly push the player UP through the ceiling.
 *
 * @param position Position to test (at eye level)
 * @param world World instance for block queries
 * @return true if feet are stuck in solid block, false if clear
 */
bool Player::checkFeetCollision(const glm::vec3& position, World* world) {
    // Player's feet position (position is at eye level)
    glm::vec3 feetPos = position - glm::vec3(0.0f, PLAYER_EYE_HEIGHT, 0.0f);
    float halfWidth = PLAYER_WIDTH / 2.0f;

    // Only check the bottom 0.5 blocks of the player (feet area)
    // This prevents head collisions from triggering the unstuck mechanism
    const float FEET_CHECK_HEIGHT = 0.5f;
    glm::vec3 minBound = feetPos - glm::vec3(halfWidth, 0.0f, halfWidth);
    glm::vec3 maxBound = feetPos + glm::vec3(halfWidth, FEET_CHECK_HEIGHT, halfWidth);

    // Convert to block coordinates
    int minX = static_cast<int>(std::floor(minBound.x));
    int minY = static_cast<int>(std::floor(minBound.y));
    int minZ = static_cast<int>(std::floor(minBound.z));
    int maxX = static_cast<int>(std::floor(maxBound.x));
    int maxY = static_cast<int>(std::floor(maxBound.y));
    int maxZ = static_cast<int>(std::floor(maxBound.z));

    // Check each block in the feet range
    for (int x = minX; x <= maxX; x++) {
        for (int y = minY; y <= maxY; y++) {
            for (int z = minZ; z <= maxZ; z++) {
                float worldX = static_cast<float>(x);
                float worldY = static_cast<float>(y);
                float worldZ = static_cast<float>(z);

                int blockID = world->getBlockAt(worldX, worldY, worldZ);

                // Check if block is solid (not air and not liquid)
                if (blockID > 0) {
                    const auto& blockDef = BlockRegistry::instance().get(blockID);
                    if (!blockDef.isLiquid) {
                        return true;  // Feet stuck in solid block
                    }
                }
            }
        }
    }

    return false;  // Feet not stuck
}

/**
 * @brief Horizontal-only AABB collision detection for ledge walking
 *
 * Ledge Walking Algorithm:
 * ========================
 *
 * This function is similar to checkCollision(), but only checks from STEP_HEIGHT
 * upward to the player's head. This allows smooth ledge walking behavior.
 *
 * Why Separate Horizontal Check?
 * -------------------------------
 * If we used full AABB checks for horizontal movement, the player would collide
 * with block edges at their feet level, preventing walking off ledges smoothly.
 *
 * Example problem WITHOUT this function:
 * - Player walks toward ledge edge
 * - Foot enters empty space beyond ledge
 * - Full AABB check sees "no collision" (no block at foot level)
 * - BUT horizontal check SHOULD allow this! (smooth ledge walking)
 * - Player would be blocked from walking off ledge edge
 *
 * Step Height Mechanic:
 * ---------------------
 * By starting collision checks at stepHeight (0.3 units = 0.6 blocks) above feet:
 * - Player can walk OFF ledges smoothly (no collision below knee)
 * - Player still collides with walls at torso/head height
 * - Creates natural "step over small obstacles" behavior
 * - Similar to Minecraft's step height mechanic
 *
 * Algorithm:
 * ----------
 * 1. Calculate AABB from knee height to head (not from feet!)
 * 2. Convert bounds to block coordinates
 * 3. Check all blocks in reduced AABB range
 * 4. Return true if any block is solid
 *
 * Visual Comparison:
 * ------------------
 * checkCollision() (full AABB):
 * ```
 *     ┌───┐         ← head
 *     │ P │
 *     │ l │
 *     │ y │
 *     │ r │         ← knees (step height)
 *     └───┘         ← feet (checks from here)
 * ```
 *
 * checkHorizontalCollision() (knee to head):
 * ```
 *     ┌───┐         ← head
 *     │ P │
 *     │ l │
 *     │ y │
 *     └───┘         ← knees (checks from here)
 *                   ← feet (NOT checked)
 * ```
 *
 * Use Cases:
 * ----------
 * - Walking off ledges: Feet can extend beyond ledge edge
 * - Walking up to walls: Torso/head still collides properly
 * - Prevents "wall climbing" by checking upper body
 * - Allows smooth transitions from ground to air (falling)
 *
 * @param position Position to test (at eye level)
 * @param world World instance for block queries
 * @return true if horizontal collision detected, false if clear
 */
bool Player::checkHorizontalCollision(const glm::vec3& position, World* world) {
    // Check collision from knee height up to head (allows walking off ledges)
    glm::vec3 feetPos = position - glm::vec3(0.0f, PLAYER_EYE_HEIGHT, 0.0f);
    float halfWidth = PLAYER_WIDTH / 2.0f;

    // Start checking from step height above feet - this is knee/step height
    using namespace PhysicsConstants;
    const float stepHeight = STEP_HEIGHT;
    glm::vec3 minBound = feetPos + glm::vec3(-halfWidth, stepHeight, -halfWidth);
    glm::vec3 maxBound = feetPos + glm::vec3(halfWidth, PLAYER_HEIGHT, halfWidth);

    // Convert to block coordinates (blocks are 1.0 units)
    int minX = (int)std::floor(minBound.x);
    int minY = (int)std::floor(minBound.y);
    int minZ = (int)std::floor(minBound.z);
    int maxX = (int)std::floor(maxBound.x);
    int maxY = (int)std::floor(maxBound.y);
    int maxZ = (int)std::floor(maxBound.z);

    // Check each block in the range
    for (int x = minX; x <= maxX; x++) {
        for (int y = minY; y <= maxY; y++) {
            for (int z = minZ; z <= maxZ; z++) {
                // Convert block coordinates back to world position
                float worldX = static_cast<float>(x);
                float worldY = static_cast<float>(y);
                float worldZ = static_cast<float>(z);

                int blockID = world->getBlockAt(worldX, worldY, worldZ);
                // Check if block is solid (not air and not liquid)
                if (blockID > 0) {
                    const auto& blockDef = BlockRegistry::instance().get(blockID);
                    if (!blockDef.isLiquid) {  // Only collide with solid blocks, not liquids
                        return true;  // Collision detected
                    }
                }
            }
        }
    }

    return false;  // No collision
}

glm::mat4 Player::getViewMatrix() const {
    if (ThirdPersonMode) {
        // Third-person camera: position behind and slightly above player
        glm::vec3 cameraPos = getCameraPosition();
        glm::vec3 lookTarget = Position; // Look at eye position
        return glm::lookAt(cameraPos, lookTarget, WorldUp);
    }
    return glm::lookAt(Position, Position + Front, Up);
}

glm::vec3 Player::getBodyPosition() const {
    // Body position is at feet level
    return Position - glm::vec3(0.0f, PLAYER_EYE_HEIGHT, 0.0f);
}

glm::vec3 Player::getCameraPosition() const {
    if (ThirdPersonMode) {
        // Camera positioned behind player, slightly above
        // Move back along the opposite of front direction
        glm::vec3 offset = -Front * ThirdPersonDistance;
        offset.y += 1.0f; // Slightly above eye level
        return Position + offset;
    }
    return Position;
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

void Player::updateBodyYawLag(float deltaTime) {
    // Inflatable T-Rex costume effect: body lags behind camera
    // Head turns immediately, body follows with spring physics when head exceeds threshold

    // Calculate angle difference between camera (Yaw) and body (m_bodyYaw)
    float yawDiff = Yaw - m_bodyYaw;

    // Normalize to [-180, 180] range
    while (yawDiff > 180.0f) yawDiff -= 360.0f;
    while (yawDiff < -180.0f) yawDiff += 360.0f;

    // Check if head has turned beyond threshold
    bool shouldRotateBody = std::abs(yawDiff) > BODY_LAG_THRESHOLD;

    if (shouldRotateBody) {
        // Spring physics: body catches up to camera with bouncy motion
        // F = k * displacement - c * velocity (underdamped for bounce)
        float springForce = BODY_LAG_SPRING * yawDiff;
        float dampingForce = BODY_LAG_DAMPING * m_bodyYawVelocity;
        float angularAccel = springForce - dampingForce;

        m_bodyYawVelocity += angularAccel * deltaTime;

        // Clamp velocity for stability
        m_bodyYawVelocity = glm::clamp(m_bodyYawVelocity, -BODY_LAG_MAX_SPEED, BODY_LAG_MAX_SPEED);
    } else {
        // Head is within threshold - apply gentle return spring and decay velocity
        // This creates a "settling" motion when returning to center
        float returnForce = BODY_LAG_SPRING * 0.3f * yawDiff;
        float returnDamping = BODY_LAG_DAMPING * 1.5f * m_bodyYawVelocity;
        m_bodyYawVelocity += (returnForce - returnDamping) * deltaTime;

        // Decay velocity when not actively turning
        m_bodyYawVelocity *= std::pow(0.92f, deltaTime * 60.0f);
    }

    // Update body yaw
    m_bodyYaw += m_bodyYawVelocity * deltaTime;

    // Normalize body yaw to [0, 360)
    while (m_bodyYaw >= 360.0f) m_bodyYaw -= 360.0f;
    while (m_bodyYaw < 0.0f) m_bodyYaw += 360.0f;
}

void Player::resetMouse() {
    m_firstMouse = true;
}

// ========== Player Persistence ==========

bool Player::savePlayerState(const std::string& worldPath) const {
    namespace fs = std::filesystem;

    try {
        // Create world directory if it doesn't exist
        fs::create_directories(worldPath);

        // Create player.dat file
        fs::path playerPath = fs::path(worldPath) / "player.dat";
        std::ofstream file(playerPath, std::ios::binary);
        if (!file.is_open()) {
            Logger::error() << "Failed to create player.dat file";
            return false;
        }

        // Write file version
        constexpr uint32_t PLAYER_FILE_VERSION = 1;
        file.write(reinterpret_cast<const char*>(&PLAYER_FILE_VERSION), sizeof(uint32_t));

        // Write position (3 floats)
        file.write(reinterpret_cast<const char*>(&Position.x), sizeof(float));
        file.write(reinterpret_cast<const char*>(&Position.y), sizeof(float));
        file.write(reinterpret_cast<const char*>(&Position.z), sizeof(float));

        // Write rotation (2 floats)
        file.write(reinterpret_cast<const char*>(&Yaw), sizeof(float));
        file.write(reinterpret_cast<const char*>(&Pitch), sizeof(float));

        // Write velocity (3 floats)
        file.write(reinterpret_cast<const char*>(&m_velocity.x), sizeof(float));
        file.write(reinterpret_cast<const char*>(&m_velocity.y), sizeof(float));
        file.write(reinterpret_cast<const char*>(&m_velocity.z), sizeof(float));

        // Write physics state (booleans as uint8_t)
        uint8_t onGround = m_onGround ? 1 : 0;
        uint8_t inLiquid = m_inLiquid ? 1 : 0;
        uint8_t cameraUnderwater = m_cameraUnderwater ? 1 : 0;
        uint8_t noclipMode = NoclipMode ? 1 : 0;
        uint8_t isSprinting = m_isSprinting ? 1 : 0;

        file.write(reinterpret_cast<const char*>(&onGround), sizeof(uint8_t));
        file.write(reinterpret_cast<const char*>(&inLiquid), sizeof(uint8_t));
        file.write(reinterpret_cast<const char*>(&cameraUnderwater), sizeof(uint8_t));
        file.write(reinterpret_cast<const char*>(&noclipMode), sizeof(uint8_t));
        file.write(reinterpret_cast<const char*>(&isSprinting), sizeof(uint8_t));

        // Write submergence
        file.write(reinterpret_cast<const char*>(&m_submergence), sizeof(float));

        // Write movement speed and mouse sensitivity
        file.write(reinterpret_cast<const char*>(&MovementSpeed), sizeof(float));
        file.write(reinterpret_cast<const char*>(&MouseSensitivity), sizeof(float));

        file.close();
        Logger::info() << "Player state saved successfully";
        return true;

    } catch (const std::exception& e) {
        Logger::error() << "Failed to save player state: " << e.what();
        return false;
    }
}

bool Player::loadPlayerState(const std::string& worldPath) {
    namespace fs = std::filesystem;

    try {
        // Check if player.dat exists
        fs::path playerPath = fs::path(worldPath) / "player.dat";
        if (!fs::exists(playerPath)) {
            Logger::info() << "player.dat not found - using default spawn";
            return false;
        }

        // Open file
        std::ifstream file(playerPath, std::ios::binary);
        if (!file.is_open()) {
            Logger::error() << "Failed to open player.dat file";
            return false;
        }

        // Read and verify version
        uint32_t version;
        file.read(reinterpret_cast<char*>(&version), sizeof(uint32_t));
        if (version != 1) {
            Logger::error() << "Unsupported player file version: " << version;
            return false;
        }

        // Read position
        file.read(reinterpret_cast<char*>(&Position.x), sizeof(float));
        file.read(reinterpret_cast<char*>(&Position.y), sizeof(float));
        file.read(reinterpret_cast<char*>(&Position.z), sizeof(float));

        // Read rotation
        file.read(reinterpret_cast<char*>(&Yaw), sizeof(float));
        file.read(reinterpret_cast<char*>(&Pitch), sizeof(float));

        // Read velocity
        file.read(reinterpret_cast<char*>(&m_velocity.x), sizeof(float));
        file.read(reinterpret_cast<char*>(&m_velocity.y), sizeof(float));
        file.read(reinterpret_cast<char*>(&m_velocity.z), sizeof(float));

        // Read physics state
        uint8_t onGround, inLiquid, cameraUnderwater, noclipMode, isSprinting;
        file.read(reinterpret_cast<char*>(&onGround), sizeof(uint8_t));
        file.read(reinterpret_cast<char*>(&inLiquid), sizeof(uint8_t));
        file.read(reinterpret_cast<char*>(&cameraUnderwater), sizeof(uint8_t));
        file.read(reinterpret_cast<char*>(&noclipMode), sizeof(uint8_t));
        file.read(reinterpret_cast<char*>(&isSprinting), sizeof(uint8_t));

        m_onGround = (onGround != 0);
        m_inLiquid = (inLiquid != 0);
        m_cameraUnderwater = (cameraUnderwater != 0);
        NoclipMode = (noclipMode != 0);
        m_isSprinting = (isSprinting != 0);

        // Read submergence
        file.read(reinterpret_cast<char*>(&m_submergence), sizeof(float));

        // Read movement speed and mouse sensitivity
        file.read(reinterpret_cast<char*>(&MovementSpeed), sizeof(float));
        file.read(reinterpret_cast<char*>(&MouseSensitivity), sizeof(float));

        file.close();

        // Update camera vectors after loading rotation
        updateVectors();

        Logger::info() << "Player state loaded successfully (pos: " << Position.x << ", "
                      << Position.y << ", " << Position.z << ")";
        return true;

    } catch (const std::exception& e) {
        Logger::error() << "Failed to load player state: " << e.what();
        return false;
    }
}
