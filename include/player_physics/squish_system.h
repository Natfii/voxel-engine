/**
 * @file squish_system.h
 * @brief Squish/deformation system for player collision response
 *
 * Creates a "squash and stretch" effect when the player collides with walls.
 * Uses spring-damper physics for natural elastic recovery.
 */

#pragma once

#include <glm/glm.hpp>
#include <vector>

// Forward declarations
class RuntimeSkeleton;
class SkeletonAnimator;

namespace PlayerPhysics {

/**
 * @brief Per-bone squish state for deformation tracking
 */
struct BoneSquishState {
    glm::vec3 currentScale;         // Current deformed scale
    glm::vec3 targetScale;          // Target scale (1.0 = rest pose)
    glm::vec3 velocity;             // Scale velocity for spring physics
    float influence;                // How much this bone is affected (0-1)

    BoneSquishState()
        : currentScale(1.0f), targetScale(1.0f), velocity(0.0f), influence(0.0f) {}

    void reset() {
        currentScale = glm::vec3(1.0f);
        targetScale = glm::vec3(1.0f);
        velocity = glm::vec3(0.0f);
        influence = 0.0f;
    }
};

/**
 * @brief Squish deformation parameters
 */
struct SquishParams {
    float springStiffness = 20.0f;      // Higher = faster recovery
    float dampingRatio = 0.5f;          // 0.4-0.6 for "squishy", 0.8+ for stiff
    float maxCompression = 0.6f;        // Minimum scale (0.6 = 40% compression max)
    float maxExpansion = 1.3f;          // Maximum scale (for overshoot/bounce)
    float influenceRadius = 1.0f;       // How far squish propagates from contact
    float impactMultiplier = 0.3f;      // Scale impact force to compression
    float recoverySpeed = 2.0f;         // Speed at which target returns to 1.0
    float volumePreservation = 0.15f;   // How much to expand perpendicular (0-0.5)
};

/**
 * @brief Manages squish/deformation for player bones
 *
 * When the player collides with surfaces, bones are compressed along the
 * collision normal. Spring-damper physics provide natural bounce-back.
 */
class SquishSystem {
public:
    SquishSystem();
    ~SquishSystem();

    /**
     * @brief Initialize with player skeleton
     * @param skeleton The player's runtime skeleton
     */
    void initialize(const RuntimeSkeleton* skeleton);

    /**
     * @brief Trigger squish deformation from a collision
     * @param contactPoint World position of collision contact
     * @param contactNormal Surface normal at collision (pointing into player)
     * @param impactForce Magnitude of impact (velocity dot normal, typically)
     */
    void onCollision(const glm::vec3& contactPoint,
                     const glm::vec3& contactNormal,
                     float impactForce);

    /**
     * @brief Update spring-damper physics each frame
     * @param deltaTime Time step in seconds
     */
    void update(float deltaTime);

    /**
     * @brief Get the deformation scale for a specific bone
     * @param boneIndex Index of the bone
     * @return Scale vector to apply to bone transform
     */
    glm::vec3 getBoneScale(int boneIndex) const;

    /**
     * @brief Apply squish scales to skeleton animator
     * @param animator The skeleton animator to modify
     *
     * This applies the squish scales as physics scale on each bone.
     */
    void applyToAnimator(SkeletonAnimator& animator);

    /**
     * @brief Reset all deformation to rest pose
     */
    void reset();

    /**
     * @brief Get/set squish parameters
     */
    SquishParams& getParams() { return m_params; }
    const SquishParams& getParams() const { return m_params; }
    void setParams(const SquishParams& params) { m_params = params; }

    /**
     * @brief Check if any bones are currently deformed
     */
    bool isDeformed() const;

    /**
     * @brief Get bone states for debugging/visualization
     */
    const std::vector<BoneSquishState>& getBoneStates() const { return m_boneStates; }

    /**
     * @brief Set bone world positions for influence calculations
     * @param positions World positions for each bone (indexed by bone index)
     */
    void setBoneWorldPositions(const std::vector<glm::vec3>& positions);

private:
    std::vector<BoneSquishState> m_boneStates;
    std::vector<glm::vec3> m_boneWorldPositions;
    const RuntimeSkeleton* m_skeleton;
    SquishParams m_params;

    /**
     * @brief Compute squish for a single bone based on collision
     */
    void computeBoneSquish(int boneIndex,
                           const glm::vec3& contactPoint,
                           const glm::vec3& contactNormal,
                           float impactForce);

    /**
     * @brief Apply spring-damper physics to a single bone
     */
    void updateBoneSpring(BoneSquishState& state, float deltaTime);
};

} // namespace PlayerPhysics
