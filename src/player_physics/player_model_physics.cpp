/**
 * @file player_model_physics.cpp
 * @brief Unified player model physics controller implementation
 */

#include "player_physics/player_model_physics.h"
#include "animation/skeleton_animator.h"
#include "world.h"
#include "logger.h"

#include <algorithm>

namespace PlayerPhysics {

PlayerModelPhysics::PlayerModelPhysics()
    : m_initialized(false)
    , m_debugDraw(false)
    , m_skeleton(nullptr)
{
    m_primaryCollision.reset();
}

PlayerModelPhysics::~PlayerModelPhysics() = default;

void PlayerModelPhysics::initialize(const RuntimeSkeleton* skeleton,
                                     const PlayerModelPhysicsConfig& config) {
    m_skeleton = skeleton;
    m_config = config;

    if (!skeleton) {
        Logger::warning() << "PlayerModelPhysics: No skeleton provided";
        return;
    }

    // Initialize subsystems
    m_centerOfGravity.initialize(skeleton);
    m_boneCollision.initialize(skeleton);
    m_squishSystem.initialize(skeleton);
    m_headTracking.initialize(skeleton);

    // Apply config to subsystems
    m_squishSystem.setParams(config.squishParams);
    m_headTracking.setParams(config.headTrackingParams);

    m_initialized = true;

    Logger::info() << "PlayerModelPhysics: Initialized with all subsystems";
}

void PlayerModelPhysics::update(float deltaTime,
                                 World* world,
                                 const glm::vec3& playerPosition,
                                 const glm::vec3& playerVelocity,
                                 const glm::vec3& lookDirection,
                                 const glm::vec3& bodyForward) {
    if (!m_initialized) return;

    // Update center of gravity position
    m_centerOfGravity.setPosition(playerPosition);
    m_centerOfGravity.setVelocity(playerVelocity);

    // Update head tracking
    if (m_config.enableHeadTracking) {
        m_headTracking.setLookDirection(lookDirection);
        m_headTracking.setBodyForward(bodyForward);
        m_headTracking.setBodyUp(glm::vec3(0.0f, 1.0f, 0.0f));
        m_headTracking.update(deltaTime);
    }

    // Check for collisions
    if (m_config.enableBoneCollision && world) {
        m_collisionResults.clear();
        int numCollisions = m_boneCollision.checkAllCollisions(world, m_collisionResults);

        if (numCollisions > 0 && m_config.enableSquish) {
            // Process collisions and trigger squish
            processCollisions(playerVelocity);
        }
    }

    // Update squish spring physics
    if (m_config.enableSquish) {
        m_squishSystem.update(deltaTime);
    }
}

void PlayerModelPhysics::updateBoneTransforms(const std::vector<glm::mat4>& boneTransforms,
                                               const glm::mat4& modelTransform) {
    if (!m_initialized) return;

    // Update bone collision capsules
    m_boneCollision.update(boneTransforms, modelTransform);

    // Update bone world positions for squish system
    if (m_config.enableSquish && m_skeleton) {
        std::vector<glm::vec3> bonePositions;
        bonePositions.reserve(boneTransforms.size());

        for (const auto& transform : boneTransforms) {
            glm::mat4 worldTransform = modelTransform * transform;
            bonePositions.push_back(glm::vec3(worldTransform[3]));
        }

        m_squishSystem.setBoneWorldPositions(bonePositions);
    }
}

bool PlayerModelPhysics::resolveCollision(World* world, glm::vec3& movement) {
    if (!m_initialized || !m_config.enableBoneCollision || !world) {
        return false;
    }

    // Check for collision with intended movement
    CollisionResult result;
    if (!m_boneCollision.checkCollision(world, result)) {
        m_primaryCollision.reset();
        return false;
    }

    m_primaryCollision = result;

    // Calculate collision response
    movement = calculateCollisionResponse(result, movement);

    return true;
}

glm::vec3 PlayerModelPhysics::calculateCollisionResponse(const CollisionResult& collision,
                                                          const glm::vec3& movement) {
    if (!collision.hasCollision) return movement;

    glm::vec3 adjustedMovement = movement;

    // Push out of collision along contact normal
    glm::vec3 pushOut = collision.contactNormal * collision.penetrationDepth;

    // Project movement onto plane perpendicular to normal (sliding)
    float dot = glm::dot(adjustedMovement, collision.contactNormal);
    if (dot < 0.0f) {
        adjustedMovement -= collision.contactNormal * dot;
    }

    // Add pushout
    adjustedMovement += pushOut;

    return adjustedMovement;
}

void PlayerModelPhysics::processCollisions(const glm::vec3& velocity) {
    if (m_collisionResults.empty()) return;

    // Find the strongest collision
    float maxPenetration = 0.0f;
    const CollisionResult* strongestCollision = nullptr;

    for (const auto& collision : m_collisionResults) {
        if (collision.penetrationDepth > maxPenetration) {
            maxPenetration = collision.penetrationDepth;
            strongestCollision = &collision;
        }
    }

    if (strongestCollision) {
        m_primaryCollision = *strongestCollision;

        // Calculate impact force based on velocity
        float impactForce = glm::length(velocity);

        // Also consider velocity direction relative to collision normal
        float velocityDot = -glm::dot(glm::normalize(velocity + glm::vec3(0.0001f)),
                                       strongestCollision->contactNormal);
        impactForce *= glm::max(velocityDot, 0.0f);

        // Trigger squish at collision point
        if (m_config.enableSquish && impactForce > 0.5f) {
            m_squishSystem.onCollision(
                strongestCollision->contactPoint,
                strongestCollision->contactNormal,
                impactForce
            );
        }
    }
}

void PlayerModelPhysics::applyToAnimator(SkeletonAnimator& animator) {
    if (!m_initialized) return;

    // Apply squish deformation
    if (m_config.enableSquish) {
        m_squishSystem.applyToAnimator(animator);
    }

    // Apply head tracking
    if (m_config.enableHeadTracking) {
        m_headTracking.applyToAnimator(animator);
    }
}

bool PlayerModelPhysics::checkGrounded(World* world) {
    if (!m_initialized || !world) return false;

    // Check leg capsules for ground contact
    const auto& capsules = m_boneCollision.getCapsules();

    for (size_t i = 0; i < capsules.size(); ++i) {
        const auto& capsule = capsules[i];

        // Only check leg capsules for ground
        if (capsule.boneName != "leg_L" && capsule.boneName != "leg_R") {
            continue;
        }

        CollisionResult result;
        if (m_boneCollision.checkCapsuleCollision(static_cast<int>(i), world, result)) {
            // Check if collision is from below (ground)
            if (result.contactNormal.y > 0.5f) {
                return true;
            }
        }
    }

    return false;
}

const CollisionResult& PlayerModelPhysics::getPrimaryCollision() const {
    return m_primaryCollision;
}

void PlayerModelPhysics::setDebugDraw(bool enabled) {
    m_debugDraw = enabled;
    m_boneCollision.setDebugDraw(enabled);
}

} // namespace PlayerPhysics
