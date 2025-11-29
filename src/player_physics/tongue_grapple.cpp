/**
 * @file tongue_grapple.cpp
 * @brief Tongue grappling hook implementation
 */

#include "player_physics/tongue_grapple.h"
#include "animation/skeleton_animator.h"
#include "world.h"
#include "block_system.h"
#include "logger.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <algorithm>

namespace PlayerPhysics {

TongueGrapple::TongueGrapple()
    : m_skeleton(nullptr)
    , m_initialized(false)
    , m_state(TongueState::IDLE)
    , m_cooldownTimer(0.0f)
    , m_shootOrigin(0.0f)
    , m_shootDirection(0.0f, 0.0f, -1.0f)
    , m_tongueTip(0.0f)
    , m_shootDistance(0.0f)
    , m_anchorPoint(0.0f)
    , m_ropeLength(0.0f)
    , m_ropeVelocity(0.0f)
{
}

TongueGrapple::~TongueGrapple() = default;

void TongueGrapple::initialize(const RuntimeSkeleton* skeleton,
                                const TongueGrappleConfig& config) {
    m_skeleton = skeleton;
    m_config = config;
    m_initialized = true;
    reset();

    Logger::info() << "TongueGrapple: Initialized (max range: " << config.maxRange
                   << " blocks, cooldown: " << config.cooldownTime << "s)";
}

void TongueGrapple::reset() {
    m_state = TongueState::IDLE;
    m_cooldownTimer = 0.0f;
    m_shootDistance = 0.0f;
    m_tongueTip = glm::vec3(0.0f);
    m_anchorPoint = glm::vec3(0.0f);
    m_ropeLength = 0.0f;
    m_ropeVelocity = glm::vec3(0.0f);
}

void TongueGrapple::update(float deltaTime,
                            World* world,
                            const glm::vec3& playerPosition,
                            glm::vec3& playerVelocity,
                            const glm::vec3& lookDirection,
                            float gravity,
                            bool isOnGround,
                            bool isInLiquid) {
    if (!m_initialized) return;

    // If we touch ground while swinging, release
    if (m_state == TongueState::ATTACHED && isOnGround) {
        release(playerVelocity);
    }

    // If we enter water while attached, release
    if (m_state == TongueState::ATTACHED && isInLiquid) {
        release(playerVelocity);
    }

    // Update based on current state
    switch (m_state) {
        case TongueState::IDLE:
            // Nothing to update, waiting for shoot input
            break;

        case TongueState::SHOOTING:
            updateShooting(deltaTime, world);
            break;

        case TongueState::ATTACHED:
            updateSwing(deltaTime, playerPosition, playerVelocity, gravity);
            break;

        case TongueState::COOLDOWN:
            updateCooldown(deltaTime);
            break;
    }
}

bool TongueGrapple::shoot(const glm::vec3& playerPosition,
                           const glm::vec3& direction,
                           World* world) {
    if (m_state != TongueState::IDLE) {
        return false;
    }

    // Start shooting
    m_shootOrigin = playerPosition;
    m_shootDirection = glm::normalize(direction);
    m_tongueTip = playerPosition;
    m_shootDistance = 0.0f;
    m_state = TongueState::SHOOTING;

    Logger::debug() << "TongueGrapple: Shooting tongue!";
    return true;
}

bool TongueGrapple::release(glm::vec3& playerVelocity) {
    if (m_state != TongueState::ATTACHED) {
        return false;
    }

    // Calculate tangential velocity (perpendicular to rope)
    glm::vec3 ropeDir = glm::normalize(m_anchorPoint - m_tongueTip);
    float radialVel = glm::dot(playerVelocity, ropeDir);

    // Keep only tangential velocity (swing momentum)
    playerVelocity = playerVelocity - radialVel * ropeDir;

    // Add upward boost for fun release
    playerVelocity.y += m_config.releaseBoost;

    // Transition to cooldown
    m_state = TongueState::COOLDOWN;
    m_cooldownTimer = m_config.cooldownTime;

    Logger::debug() << "TongueGrapple: Released! Velocity: " << glm::length(playerVelocity);
    return true;
}

void TongueGrapple::updateShooting(float deltaTime, World* world) {
    // Advance tongue tip
    float travelThisFrame = m_config.tongueSpeed * deltaTime;
    m_shootDistance += travelThisFrame;
    m_tongueTip = m_shootOrigin + m_shootDirection * m_shootDistance;

    // Check if exceeded max range
    if (m_shootDistance >= m_config.maxRange) {
        // Missed! Return to idle
        m_state = TongueState::IDLE;
        Logger::debug() << "TongueGrapple: Missed (max range)";
        return;
    }

    // Raycast to check for hit
    glm::vec3 hitPoint, hitNormal;
    if (castTongueRay(world, m_shootOrigin, m_shootDirection,
                      m_shootDistance, hitPoint, hitNormal)) {
        // Hit a solid block!
        m_anchorPoint = hitPoint;
        m_ropeLength = glm::length(hitPoint - m_shootOrigin);
        m_tongueTip = hitPoint;
        m_ropeVelocity = glm::vec3(0.0f);
        m_state = TongueState::ATTACHED;

        Logger::debug() << "TongueGrapple: Attached! Rope length: " << m_ropeLength;
    }
}

void TongueGrapple::updateSwing(float deltaTime,
                                 const glm::vec3& playerPos,
                                 glm::vec3& playerVelocity,
                                 float gravity) {
    // Update tongue tip to follow player (for rendering)
    m_tongueTip = playerPos;

    // Calculate rope direction and current distance
    glm::vec3 toAnchor = m_anchorPoint - playerPos;
    float currentDist = glm::length(toAnchor);

    if (currentDist < 0.01f) {
        // Too close to anchor, release
        release(playerVelocity);
        return;
    }

    glm::vec3 ropeDir = toAnchor / currentDist;

    // ========== PENDULUM PHYSICS ==========

    // 1. Apply gravity (scaled for fun factor)
    playerVelocity.y -= gravity * m_config.gravityScale * deltaTime;

    // 2. Calculate radial velocity (velocity toward/away from anchor)
    float radialVel = glm::dot(playerVelocity, ropeDir);

    // 3. If moving away from anchor and beyond rope length, constrain
    if (currentDist > m_ropeLength && radialVel < 0.0f) {
        // Remove outward velocity (rope is taut)
        playerVelocity -= radialVel * ropeDir;

        // Apply spring force pulling back (gives bouncy feel)
        float extension = currentDist - m_ropeLength;
        float springForce = extension * m_config.ropeSpring;
        playerVelocity += ropeDir * springForce * deltaTime;
    }

    // 4. Apply damping (energy loss for stability)
    float dampFactor = std::pow(m_config.ropeDamping, deltaTime);
    playerVelocity *= dampFactor;

    // 5. Clamp max velocity
    float speed = glm::length(playerVelocity);
    if (speed > m_config.maxSwingSpeed) {
        playerVelocity *= m_config.maxSwingSpeed / speed;
    }

    // 6. Check if rope stretched too far (broken)
    if (currentDist > m_ropeLength * 2.0f) {
        Logger::debug() << "TongueGrapple: Rope broke (stretched too far)";
        release(playerVelocity);
    }
}

void TongueGrapple::updateCooldown(float deltaTime) {
    m_cooldownTimer -= deltaTime;
    if (m_cooldownTimer <= 0.0f) {
        m_cooldownTimer = 0.0f;
        m_state = TongueState::IDLE;
        Logger::debug() << "TongueGrapple: Cooldown complete, ready to shoot";
    }
}

bool TongueGrapple::castTongueRay(World* world,
                                   const glm::vec3& origin,
                                   const glm::vec3& direction,
                                   float maxDist,
                                   glm::vec3& hitPoint,
                                   glm::vec3& hitNormal) {
    if (!world) return false;

    // Simple DDA raycast through voxels
    glm::vec3 rayPos = origin;
    glm::vec3 rayDir = glm::normalize(direction);

    // Step size for ray marching (smaller = more accurate but slower)
    const float stepSize = 0.1f;
    float distTraveled = 0.0f;

    while (distTraveled < maxDist) {
        // Advance ray
        rayPos += rayDir * stepSize;
        distTraveled += stepSize;

        // Check block at ray position
        int blockX = static_cast<int>(std::floor(rayPos.x));
        int blockY = static_cast<int>(std::floor(rayPos.y));
        int blockZ = static_cast<int>(std::floor(rayPos.z));

        int blockID = world->getBlockAt(
            static_cast<float>(blockX),
            static_cast<float>(blockY),
            static_cast<float>(blockZ)
        );

        // Check if solid block (not air, not liquid)
        if (blockID > 0) {
            const auto& blockDef = BlockRegistry::instance().get(blockID);
            if (!blockDef.isLiquid) {
                // Hit solid block!
                hitPoint = rayPos;

                // Calculate hit normal (simplified - just use dominant axis)
                glm::vec3 blockCenter(blockX + 0.5f, blockY + 0.5f, blockZ + 0.5f);
                glm::vec3 toHit = hitPoint - blockCenter;
                glm::vec3 absHit = glm::abs(toHit);

                if (absHit.x > absHit.y && absHit.x > absHit.z) {
                    hitNormal = glm::vec3(toHit.x > 0 ? 1.0f : -1.0f, 0.0f, 0.0f);
                } else if (absHit.y > absHit.z) {
                    hitNormal = glm::vec3(0.0f, toHit.y > 0 ? 1.0f : -1.0f, 0.0f);
                } else {
                    hitNormal = glm::vec3(0.0f, 0.0f, toHit.z > 0 ? 1.0f : -1.0f);
                }

                return true;
            }
        }
    }

    return false;
}

glm::vec3 TongueGrapple::calculateRopeForce(const glm::vec3& playerPos,
                                             const glm::vec3& playerVel) {
    glm::vec3 toAnchor = m_anchorPoint - playerPos;
    float dist = glm::length(toAnchor);

    if (dist < 0.01f) return glm::vec3(0.0f);

    glm::vec3 ropeDir = toAnchor / dist;

    // Spring force when stretched beyond rope length
    if (dist > m_ropeLength) {
        float extension = dist - m_ropeLength;
        return ropeDir * extension * m_config.ropeSpring;
    }

    return glm::vec3(0.0f);
}

} // namespace PlayerPhysics
