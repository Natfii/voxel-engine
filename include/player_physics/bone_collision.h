/**
 * @file bone_collision.h
 * @brief Per-bone collision capsule system for player character
 *
 * Provides accurate skinned mesh collision by attaching capsules to each bone.
 * Capsules follow skeletal animation and detect collision with voxel terrain.
 */

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <string>

// Forward declarations
class World;
class RuntimeSkeleton;

namespace PlayerPhysics {

/**
 * @brief Collision capsule attached to a bone
 */
struct BoneCollisionCapsule {
    std::string boneName;           // Name of the bone this capsule is attached to
    int boneIndex;                  // Index into skeleton bone array
    float radius;                   // Capsule radius
    float height;                   // Capsule height (length along bone axis)
    glm::vec3 localOffset;          // Offset from bone position in local space
    glm::vec3 localDirection;       // Direction of capsule in local space (default: along Y)

    // Computed world-space values (updated each frame)
    glm::vec3 worldStart;           // Capsule start point in world space
    glm::vec3 worldEnd;             // Capsule end point in world space
    glm::vec3 worldCenter;          // Capsule center for quick AABB tests

    BoneCollisionCapsule()
        : boneIndex(-1), radius(0.1f), height(0.2f),
          localOffset(0.0f), localDirection(0.0f, 1.0f, 0.0f) {}
};

/**
 * @brief Result of a collision test
 */
struct CollisionResult {
    bool hasCollision = false;          // Whether collision occurred
    glm::vec3 contactPoint;             // Point of contact in world space
    glm::vec3 contactNormal;            // Normal pointing out of colliding surface
    float penetrationDepth;             // How deep the penetration is
    int capsuleIndex;                   // Which capsule collided
    glm::ivec3 blockPosition;           // Position of the voxel block hit

    void reset() {
        hasCollision = false;
        contactPoint = glm::vec3(0.0f);
        contactNormal = glm::vec3(0.0f, 1.0f, 0.0f);
        penetrationDepth = 0.0f;
        capsuleIndex = -1;
        blockPosition = glm::ivec3(0);
    }
};

/**
 * @brief Manages collision capsules for all bones in the player skeleton
 */
class BoneCollisionManager {
public:
    BoneCollisionManager();
    ~BoneCollisionManager();

    /**
     * @brief Initialize collision system with player skeleton
     * @param skeleton The player's runtime skeleton
     *
     * Creates appropriate capsules for each bone based on model proportions.
     * Bones: spine_root, spine_tip, head, leg_L, leg_R, arm_L, arm_R, tail_base, tail_tip
     */
    void initialize(const RuntimeSkeleton* skeleton);

    /**
     * @brief Update capsule world positions from bone transforms
     * @param boneTransforms Array of world-space bone transformation matrices
     * @param modelTransform The model's world transform (position, rotation, scale)
     */
    void update(const std::vector<glm::mat4>& boneTransforms,
                const glm::mat4& modelTransform);

    /**
     * @brief Check collision against voxel world
     * @param world The voxel world to test against
     * @param result Output collision information
     * @return True if any collision detected
     */
    bool checkCollision(World* world, CollisionResult& result);

    /**
     * @brief Check collision for a specific capsule
     * @param capsuleIndex Index of the capsule to test
     * @param world The voxel world
     * @param result Output collision information
     * @return True if collision detected
     */
    bool checkCapsuleCollision(int capsuleIndex, World* world, CollisionResult& result);

    /**
     * @brief Get all collision results (for multi-contact response)
     * @param world The voxel world
     * @param results Output vector of all collisions
     * @return Number of collisions found
     */
    int checkAllCollisions(World* world, std::vector<CollisionResult>& results);

    /**
     * @brief Get the collision capsules
     */
    const std::vector<BoneCollisionCapsule>& getCapsules() const { return m_capsules; }

    /**
     * @brief Get a specific capsule by bone name
     */
    const BoneCollisionCapsule* getCapsuleByBone(const std::string& boneName) const;

    /**
     * @brief Enable/disable debug visualization
     */
    void setDebugDraw(bool enabled) { m_debugDraw = enabled; }
    bool isDebugDrawEnabled() const { return m_debugDraw; }

    /**
     * @brief Get combined AABB of all capsules for broad-phase culling
     */
    void getBoundingBox(glm::vec3& outMin, glm::vec3& outMax) const;

private:
    std::vector<BoneCollisionCapsule> m_capsules;
    const RuntimeSkeleton* m_skeleton;
    bool m_debugDraw;

    // Bounding box cache
    glm::vec3 m_boundsMin;
    glm::vec3 m_boundsMax;

    /**
     * @brief Create default capsule configuration for player model
     */
    void createDefaultCapsules();

    /**
     * @brief Test capsule-AABB intersection (capsule vs voxel block)
     */
    bool capsuleAABBIntersection(const BoneCollisionCapsule& capsule,
                                  const glm::vec3& boxMin,
                                  const glm::vec3& boxMax,
                                  glm::vec3& contactPoint,
                                  glm::vec3& contactNormal,
                                  float& penetration);

    /**
     * @brief Find closest point on capsule line segment to a point
     */
    glm::vec3 closestPointOnCapsule(const BoneCollisionCapsule& capsule,
                                     const glm::vec3& point);
};

} // namespace PlayerPhysics
