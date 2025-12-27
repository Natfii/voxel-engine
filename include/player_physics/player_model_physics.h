/**
 * @file player_model_physics.h
 * @brief Unified player model physics controller
 *
 * Combines all physics systems for the player character:
 * - Center of gravity at spine_root
 * - Per-bone collision capsules
 * - Squish/deformation on collision
 * - Head tracking following look direction
 *
 * This replaces the simple AABB collision system in Player class.
 */

#pragma once

#include "player_physics/center_of_gravity.h"
#include "player_physics/bone_collision.h"
#include "player_physics/squish_system.h"
#include "player_physics/head_tracking.h"

#include <glm/glm.hpp>
#include <memory>

// Forward declarations
class World;
class SkeletonAnimator;
class RuntimeSkeleton;

namespace PlayerPhysics {

/**
 * @brief Configuration for player model physics
 */
struct PlayerModelPhysicsConfig {
    // Collision settings
    bool enableBoneCollision = true;    // Use per-bone capsules instead of AABB
    bool enableSquish = true;           // Enable squish deformation on collision
    bool enableHeadTracking = true;     // Enable procedural head look

    // Physics settings
    float gravity = 32.0f;              // Gravity acceleration
    float terminalVelocity = -80.0f;    // Maximum fall speed
    float groundCheckDistance = 0.05f;  // Distance to check for ground

    // Squish parameters (can also use SquishSystem::getParams())
    SquishParams squishParams;

    // Head tracking parameters
    HeadTrackingParams headTrackingParams;
};

/**
 * @brief Unified physics controller for player model
 *
 * Manages all physics systems for the skinned player mesh:
 * - Bone-based collision detection against voxel terrain
 * - Squish deformation when colliding with walls
 * - Head/neck procedural animation following look direction
 * - Center of gravity at spine_root for natural physics feel
 */
class PlayerModelPhysics {
public:
    PlayerModelPhysics();
    ~PlayerModelPhysics();

    /**
     * @brief Initialize all physics systems
     * @param skeleton The player's runtime skeleton
     * @param config Physics configuration
     */
    void initialize(const RuntimeSkeleton* skeleton,
                   const PlayerModelPhysicsConfig& config = PlayerModelPhysicsConfig());

    /**
     * @brief Update physics each frame
     * @param deltaTime Time step in seconds
     * @param world Voxel world for collision detection
     * @param playerPosition Current player world position
     * @param playerVelocity Current player velocity
     * @param lookDirection Direction player is looking (for head tracking)
     * @param bodyForward Player body forward direction
     */
    void update(float deltaTime,
                World* world,
                const glm::vec3& playerPosition,
                const glm::vec3& playerVelocity,
                const glm::vec3& lookDirection,
                const glm::vec3& bodyForward);

    /**
     * @brief Update bone transforms from skeleton animator
     * @param boneTransforms Current bone world transforms
     * @param modelTransform Player model world transform
     */
    void updateBoneTransforms(const std::vector<glm::mat4>& boneTransforms,
                              const glm::mat4& modelTransform);

    /**
     * @brief Check collision and return collision response
     * @param world Voxel world
     * @param movement Intended movement vector (modified if collision)
     * @return True if collision occurred
     */
    bool resolveCollision(World* world, glm::vec3& movement);

    /**
     * @brief Apply physics effects to skeleton animator
     * @param animator Skeleton animator to modify
     *
     * Applies squish scales and head tracking rotations.
     */
    void applyToAnimator(SkeletonAnimator& animator);

    /**
     * @brief Check if player is on ground using bone collision
     * @param world Voxel world
     * @return True if any foot/leg capsule is touching ground
     */
    bool checkGrounded(World* world);

    /**
     * @brief Get all current collision results
     */
    const std::vector<CollisionResult>& getCollisionResults() const { return m_collisionResults; }

    /**
     * @brief Get the strongest collision (deepest penetration)
     */
    const CollisionResult& getPrimaryCollision() const;

    // Subsystem access
    CenterOfGravity& getCenterOfGravity() { return m_centerOfGravity; }
    BoneCollisionManager& getBoneCollision() { return m_boneCollision; }
    SquishSystem& getSquishSystem() { return m_squishSystem; }
    HeadTracking& getHeadTracking() { return m_headTracking; }

    const CenterOfGravity& getCenterOfGravity() const { return m_centerOfGravity; }
    const BoneCollisionManager& getBoneCollision() const { return m_boneCollision; }
    const SquishSystem& getSquishSystem() const { return m_squishSystem; }
    const HeadTracking& getHeadTracking() const { return m_headTracking; }

    // Configuration
    PlayerModelPhysicsConfig& getConfig() { return m_config; }
    const PlayerModelPhysicsConfig& getConfig() const { return m_config; }

    /**
     * @brief Enable/disable debug visualization
     */
    void setDebugDraw(bool enabled);
    bool isDebugDrawEnabled() const { return m_debugDraw; }

    /**
     * @brief Check if system is initialized
     */
    bool isInitialized() const { return m_initialized; }

private:
    // Subsystems
    CenterOfGravity m_centerOfGravity;
    BoneCollisionManager m_boneCollision;
    SquishSystem m_squishSystem;
    HeadTracking m_headTracking;

    // State
    PlayerModelPhysicsConfig m_config;
    std::vector<CollisionResult> m_collisionResults;
    CollisionResult m_primaryCollision;
    bool m_initialized;
    bool m_debugDraw;

    const RuntimeSkeleton* m_skeleton;

    /**
     * @brief Process collision results and trigger squish
     */
    void processCollisions(const glm::vec3& velocity);

    /**
     * @brief Calculate collision response movement adjustment
     */
    glm::vec3 calculateCollisionResponse(const CollisionResult& collision,
                                          const glm::vec3& movement);
};

} // namespace PlayerPhysics
