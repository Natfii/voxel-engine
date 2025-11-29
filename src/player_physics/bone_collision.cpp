/**
 * @file bone_collision.cpp
 * @brief Per-bone collision capsule implementation
 */

#include "player_physics/bone_collision.h"
#include "animation/skeleton_animator.h"
#include "world.h"
#include "block_system.h"
#include "logger.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace PlayerPhysics {

BoneCollisionManager::BoneCollisionManager()
    : m_skeleton(nullptr)
    , m_debugDraw(false)
    , m_boundsMin(0.0f)
    , m_boundsMax(0.0f)
{
}

BoneCollisionManager::~BoneCollisionManager() = default;

void BoneCollisionManager::initialize(const RuntimeSkeleton* skeleton) {
    m_skeleton = skeleton;
    m_capsules.clear();

    if (!skeleton) {
        Logger::warning() << "BoneCollisionManager: No skeleton provided";
        return;
    }

    createDefaultCapsules();

    Logger::info() << "BoneCollisionManager: Initialized with " << m_capsules.size()
                   << " collision capsules for " << skeleton->bones.size() << " bones";
}

void BoneCollisionManager::createDefaultCapsules() {
    if (!m_skeleton) return;

    // Create capsules for each bone based on typical proportions
    // Bones: spine_root, spine_tip, head, leg_L, leg_R, arm_L, arm_R, tail_base, tail_tip

    struct CapsuleDef {
        const char* boneName;
        float radius;
        float height;
        glm::vec3 offset;
    };

    // Capsule definitions - tuned for player model proportions
    const CapsuleDef definitions[] = {
        // Main body
        {"spine_root",  0.15f, 0.30f, glm::vec3(0.0f, 0.0f, 0.0f)},
        {"spine_tip",   0.12f, 0.25f, glm::vec3(0.0f, 0.0f, 0.0f)},
        {"head",        0.10f, 0.15f, glm::vec3(0.0f, 0.0f, 0.0f)},

        // Limbs
        {"leg_L",       0.05f, 0.25f, glm::vec3(0.0f, -0.1f, 0.0f)},
        {"leg_R",       0.05f, 0.25f, glm::vec3(0.0f, -0.1f, 0.0f)},
        {"arm_L",       0.04f, 0.20f, glm::vec3(0.0f, 0.0f, 0.0f)},
        {"arm_R",       0.04f, 0.20f, glm::vec3(0.0f, 0.0f, 0.0f)},

        // Tail
        {"tail_base",   0.06f, 0.20f, glm::vec3(0.0f, 0.0f, 0.0f)},
        {"tail_tip",    0.04f, 0.25f, glm::vec3(0.0f, 0.0f, 0.0f)},
    };

    for (const auto& def : definitions) {
        int boneIndex = m_skeleton->findBone(def.boneName);
        if (boneIndex >= 0) {
            BoneCollisionCapsule capsule;
            capsule.boneName = def.boneName;
            capsule.boneIndex = boneIndex;
            capsule.radius = def.radius;
            capsule.height = def.height;
            capsule.localOffset = def.offset;
            capsule.localDirection = glm::vec3(0.0f, 1.0f, 0.0f);
            m_capsules.push_back(capsule);
        }
    }
}

void BoneCollisionManager::update(const std::vector<glm::mat4>& boneTransforms,
                                   const glm::mat4& modelTransform) {
    if (m_capsules.empty() || boneTransforms.empty()) return;

    // Reset bounds
    m_boundsMin = glm::vec3(std::numeric_limits<float>::max());
    m_boundsMax = glm::vec3(std::numeric_limits<float>::lowest());

    for (auto& capsule : m_capsules) {
        if (capsule.boneIndex < 0 ||
            capsule.boneIndex >= static_cast<int>(boneTransforms.size())) {
            continue;
        }

        // Get bone world transform
        glm::mat4 boneWorld = modelTransform * boneTransforms[capsule.boneIndex];

        // Extract bone position
        glm::vec3 bonePos = glm::vec3(boneWorld[3]);

        // Transform local direction to world space
        glm::vec3 worldDir = glm::normalize(glm::vec3(boneWorld * glm::vec4(capsule.localDirection, 0.0f)));

        // Apply local offset
        glm::vec3 offset = glm::vec3(boneWorld * glm::vec4(capsule.localOffset, 0.0f));
        bonePos += offset;

        // Calculate capsule endpoints
        float halfHeight = capsule.height * 0.5f;
        capsule.worldStart = bonePos - worldDir * halfHeight;
        capsule.worldEnd = bonePos + worldDir * halfHeight;
        capsule.worldCenter = bonePos;

        // Update bounds
        glm::vec3 capsuleMin = glm::min(capsule.worldStart, capsule.worldEnd) - glm::vec3(capsule.radius);
        glm::vec3 capsuleMax = glm::max(capsule.worldStart, capsule.worldEnd) + glm::vec3(capsule.radius);

        m_boundsMin = glm::min(m_boundsMin, capsuleMin);
        m_boundsMax = glm::max(m_boundsMax, capsuleMax);
    }
}

bool BoneCollisionManager::checkCollision(World* world, CollisionResult& result) {
    result.reset();

    float deepestPenetration = 0.0f;

    for (size_t i = 0; i < m_capsules.size(); ++i) {
        CollisionResult capsuleResult;
        if (checkCapsuleCollision(static_cast<int>(i), world, capsuleResult)) {
            if (capsuleResult.penetrationDepth > deepestPenetration) {
                deepestPenetration = capsuleResult.penetrationDepth;
                result = capsuleResult;
            }
        }
    }

    return result.hasCollision;
}

bool BoneCollisionManager::checkCapsuleCollision(int capsuleIndex, World* world,
                                                  CollisionResult& result) {
    result.reset();

    if (capsuleIndex < 0 || capsuleIndex >= static_cast<int>(m_capsules.size())) {
        return false;
    }

    const BoneCollisionCapsule& capsule = m_capsules[capsuleIndex];

    // Get capsule AABB for broad phase
    glm::vec3 capsuleMin = glm::min(capsule.worldStart, capsule.worldEnd) - glm::vec3(capsule.radius);
    glm::vec3 capsuleMax = glm::max(capsule.worldStart, capsule.worldEnd) + glm::vec3(capsule.radius);

    // Convert to block coordinates
    int minX = static_cast<int>(std::floor(capsuleMin.x));
    int minY = static_cast<int>(std::floor(capsuleMin.y));
    int minZ = static_cast<int>(std::floor(capsuleMin.z));
    int maxX = static_cast<int>(std::floor(capsuleMax.x));
    int maxY = static_cast<int>(std::floor(capsuleMax.y));
    int maxZ = static_cast<int>(std::floor(capsuleMax.z));

    float deepestPenetration = 0.0f;

    // Check all blocks in range
    for (int x = minX; x <= maxX; ++x) {
        for (int y = minY; y <= maxY; ++y) {
            for (int z = minZ; z <= maxZ; ++z) {
                int blockID = world->getBlockAt(static_cast<float>(x),
                                                 static_cast<float>(y),
                                                 static_cast<float>(z));

                // Check if solid block (not air, not liquid)
                if (blockID > 0) {
                    const auto& blockDef = BlockRegistry::instance().get(blockID);
                    if (blockDef.isLiquid) continue;

                    // Block AABB (blocks are 1.0 unit cubes)
                    glm::vec3 blockMin(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
                    glm::vec3 blockMax = blockMin + glm::vec3(1.0f);

                    glm::vec3 contactPoint, contactNormal;
                    float penetration;

                    if (capsuleAABBIntersection(capsule, blockMin, blockMax,
                                                 contactPoint, contactNormal, penetration)) {
                        if (penetration > deepestPenetration) {
                            deepestPenetration = penetration;
                            result.hasCollision = true;
                            result.contactPoint = contactPoint;
                            result.contactNormal = contactNormal;
                            result.penetrationDepth = penetration;
                            result.capsuleIndex = capsuleIndex;
                            result.blockPosition = glm::ivec3(x, y, z);
                        }
                    }
                }
            }
        }
    }

    return result.hasCollision;
}

int BoneCollisionManager::checkAllCollisions(World* world,
                                              std::vector<CollisionResult>& results) {
    results.clear();

    for (size_t i = 0; i < m_capsules.size(); ++i) {
        CollisionResult result;
        if (checkCapsuleCollision(static_cast<int>(i), world, result)) {
            results.push_back(result);
        }
    }

    return static_cast<int>(results.size());
}

const BoneCollisionCapsule* BoneCollisionManager::getCapsuleByBone(
    const std::string& boneName) const {
    for (const auto& capsule : m_capsules) {
        if (capsule.boneName == boneName) {
            return &capsule;
        }
    }
    return nullptr;
}

void BoneCollisionManager::getBoundingBox(glm::vec3& outMin, glm::vec3& outMax) const {
    outMin = m_boundsMin;
    outMax = m_boundsMax;
}

glm::vec3 BoneCollisionManager::closestPointOnCapsule(const BoneCollisionCapsule& capsule,
                                                       const glm::vec3& point) {
    // Find closest point on the capsule's line segment
    glm::vec3 ab = capsule.worldEnd - capsule.worldStart;
    float t = glm::dot(point - capsule.worldStart, ab) / glm::dot(ab, ab);
    t = glm::clamp(t, 0.0f, 1.0f);
    return capsule.worldStart + t * ab;
}

bool BoneCollisionManager::capsuleAABBIntersection(const BoneCollisionCapsule& capsule,
                                                    const glm::vec3& boxMin,
                                                    const glm::vec3& boxMax,
                                                    glm::vec3& contactPoint,
                                                    glm::vec3& contactNormal,
                                                    float& penetration) {
    // Find closest point on capsule line segment to box
    glm::vec3 boxCenter = (boxMin + boxMax) * 0.5f;
    glm::vec3 boxHalfSize = (boxMax - boxMin) * 0.5f;

    // Closest point on capsule axis to box center
    glm::vec3 closestOnLine = closestPointOnCapsule(capsule, boxCenter);

    // Clamp to box to find closest point on box
    glm::vec3 closestOnBox = glm::clamp(closestOnLine, boxMin, boxMax);

    // Distance from capsule line to box surface
    glm::vec3 delta = closestOnLine - closestOnBox;
    float distSq = glm::dot(delta, delta);
    float radiusSq = capsule.radius * capsule.radius;

    if (distSq < radiusSq) {
        float dist = std::sqrt(distSq);
        penetration = capsule.radius - dist;

        if (dist > 0.0001f) {
            contactNormal = delta / dist;
        } else {
            // Inside box, determine face normal
            glm::vec3 localPoint = closestOnLine - boxCenter;
            glm::vec3 absLocal = glm::abs(localPoint);

            if (absLocal.x > absLocal.y && absLocal.x > absLocal.z) {
                contactNormal = glm::vec3(localPoint.x > 0 ? 1.0f : -1.0f, 0.0f, 0.0f);
            } else if (absLocal.y > absLocal.z) {
                contactNormal = glm::vec3(0.0f, localPoint.y > 0 ? 1.0f : -1.0f, 0.0f);
            } else {
                contactNormal = glm::vec3(0.0f, 0.0f, localPoint.z > 0 ? 1.0f : -1.0f);
            }
            penetration = capsule.radius;
        }

        contactPoint = closestOnBox;
        return true;
    }

    return false;
}

} // namespace PlayerPhysics
