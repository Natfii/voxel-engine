/**
 * @file tongue_grapple.h
 * @brief Tongue grappling hook system for gecko player
 *
 * Allows player to shoot a pink tongue that sticks to blocks and
 * swing like Spider-Man. Creates a fun, physics-based traversal mechanic.
 *
 * Usage:
 * - Press jump while in air (not water) to shoot tongue toward cursor
 * - Tongue travels max 25 blocks, sticks to first solid block
 * - Player swings on tongue with pendulum physics
 * - Press jump again to release, keeping momentum
 * - 0.5 second cooldown before can shoot again
 */

#pragma once

#include <glm/glm.hpp>

// Forward declarations
class World;
class RuntimeSkeleton;

namespace PlayerPhysics {

/**
 * @brief Tongue grapple state enumeration
 */
enum class TongueState {
    IDLE,       ///< Ready to shoot
    SHOOTING,   ///< Tongue traveling to target
    ATTACHED,   ///< Swinging on tongue
    COOLDOWN    ///< Recently released, waiting 0.5s
};

/**
 * @brief Configuration for tongue grappling physics
 */
struct TongueGrappleConfig {
    // Shooting
    float tongueSpeed = 60.0f;          ///< Tongue travel speed (blocks/sec)
    float maxRange = 25.0f;             ///< Maximum tongue length (blocks)
    float cooldownTime = 0.5f;          ///< Time before can shoot again (sec)

    // Reel-in
    float reelSpeed = 8.0f;             ///< How fast rope shortens when reeling (blocks/sec)
    float minRopeLength = 2.0f;         ///< Minimum rope length when fully reeled

    // Swing physics (tuned for fun, bouncy feel)
    float ropeSpring = 12.0f;           ///< Spring constant for rope tension
    float ropeDamping = 0.6f;           ///< Damping ratio (< 1 = bouncy)
    float gravityScale = 0.8f;          ///< How much gravity affects swing
    float maxSwingSpeed = 35.0f;        ///< Maximum swing velocity

    // Release
    float releaseBoost = 4.0f;          ///< Upward boost when releasing mid-swing
};

/**
 * @brief Manages tongue grappling mechanics for gecko player
 *
 * State Machine:
 * IDLE → (jump in air) → SHOOTING → (hit block) → ATTACHED → (jump) → COOLDOWN → IDLE
 *                              ↓ (miss)
 *                            IDLE
 */
class TongueGrapple {
public:
    TongueGrapple();
    ~TongueGrapple();

    /**
     * @brief Initialize the tongue system
     * @param skeleton Player skeleton (for head bone position)
     * @param config Physics configuration
     */
    void initialize(const RuntimeSkeleton* skeleton,
                    const TongueGrappleConfig& config = TongueGrappleConfig());

    /**
     * @brief Update tongue physics each frame
     * @param deltaTime Time step
     * @param world World for raycasting
     * @param playerPosition Player eye position
     * @param playerVelocity Player velocity (modified by swing physics)
     * @param lookDirection Camera look direction (where to shoot)
     * @param gravity Gravity constant
     * @param isOnGround True if player on ground
     * @param isInLiquid True if player in water
     */
    void update(float deltaTime,
                World* world,
                const glm::vec3& playerPosition,
                glm::vec3& playerVelocity,
                const glm::vec3& lookDirection,
                float gravity,
                bool isOnGround,
                bool isInLiquid);

    /**
     * @brief Try to shoot tongue (call when jump pressed in air)
     * @param playerPosition Starting position
     * @param direction Direction to shoot
     * @param world World for raycast
     * @return True if shot was started
     */
    bool shoot(const glm::vec3& playerPosition,
               const glm::vec3& direction,
               World* world);

    /**
     * @brief Release tongue and keep momentum
     * @param playerVelocity Modified with release boost
     * @return True if was attached and released
     */
    bool release(glm::vec3& playerVelocity);

    /**
     * @brief Reel in the rope (shorten it) for momentum gain
     * @param deltaTime Frame time
     * @param playerPos Current player position
     * Call this while holding left-click during swing
     */
    void reelIn(float deltaTime, const glm::vec3& playerPos);

    // ========== Getters ==========

    TongueState getState() const { return m_state; }
    bool canShoot() const { return m_state == TongueState::IDLE; }
    bool isAttached() const { return m_state == TongueState::ATTACHED; }
    bool isShooting() const { return m_state == TongueState::SHOOTING; }

    /// Get tongue tip position (for rendering)
    glm::vec3 getTongueTip() const { return m_tongueTip; }

    /// Get tongue origin position (mouth - for rendering)
    glm::vec3 getTongueOrigin() const { return m_shootOrigin; }

    /// Get anchor point where tongue is stuck
    glm::vec3 getAnchor() const { return m_anchorPoint; }

    /// Check if tongue should be rendered (shooting or attached)
    bool shouldRender() const { return m_state == TongueState::SHOOTING || m_state == TongueState::ATTACHED; }

    /// Get current rope length
    float getRopeLength() const { return m_ropeLength; }

    /// Get tongue extension (0-1 during shooting)
    float getExtension() const { return m_shootDistance / m_config.maxRange; }

    /// Get cooldown remaining (0 if ready)
    float getCooldown() const { return m_cooldownTimer; }

    /// Reset to idle state
    void reset();

    /// Configuration access
    TongueGrappleConfig& getConfig() { return m_config; }
    const TongueGrappleConfig& getConfig() const { return m_config; }

private:
    TongueGrappleConfig m_config;
    const RuntimeSkeleton* m_skeleton;
    bool m_initialized;

    // State
    TongueState m_state;
    float m_cooldownTimer;

    // Shooting state
    glm::vec3 m_shootOrigin;
    glm::vec3 m_shootDirection;
    glm::vec3 m_tongueTip;
    float m_shootDistance;

    // Attached state
    glm::vec3 m_anchorPoint;
    float m_ropeLength;
    glm::vec3 m_ropeVelocity;

    // Helper methods
    bool castTongueRay(World* world, const glm::vec3& origin,
                       const glm::vec3& direction, float maxDist,
                       glm::vec3& hitPoint, glm::vec3& hitNormal);

    void updateShooting(float deltaTime, World* world);
    void updateSwing(float deltaTime, const glm::vec3& playerPos,
                     glm::vec3& playerVelocity, float gravity);
    void updateCooldown(float deltaTime);

    glm::vec3 calculateRopeForce(const glm::vec3& playerPos,
                                  const glm::vec3& playerVel);
};

} // namespace PlayerPhysics
