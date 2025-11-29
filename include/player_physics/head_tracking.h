/**
 * @file head_tracking.h
 * @brief Procedural head tracking system for player character
 *
 * Makes the player's head follow the look direction in real-time.
 * Blends with existing animations for natural head movement.
 */

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Forward declarations
class RuntimeSkeleton;
class SkeletonAnimator;

namespace PlayerPhysics {

/**
 * @brief Head tracking parameters
 */
struct HeadTrackingParams {
    float maxYawAngle = 70.0f;          // Maximum horizontal rotation (degrees)
    float maxPitchAngle = 45.0f;        // Maximum vertical rotation (degrees)
    float trackingSpeed = 8.0f;         // How fast head follows target (higher = snappier)
    float returnSpeed = 4.0f;           // How fast head returns when not tracking
    float blendWeight = 1.0f;           // Blend with animation (0 = animation only, 1 = full tracking)
    bool enableNeckBlend = true;        // Also rotate spine_tip slightly for natural look
    float neckBlendRatio = 0.3f;        // How much neck rotates vs head (0-1)
};

/**
 * @brief Manages procedural head rotation to follow look direction
 */
class HeadTracking {
public:
    HeadTracking();
    ~HeadTracking();

    /**
     * @brief Initialize with player skeleton
     * @param skeleton The player's runtime skeleton
     */
    void initialize(const RuntimeSkeleton* skeleton);

    /**
     * @brief Set the target look direction
     * @param direction Normalized direction vector in world space
     *
     * This is typically the player's Front vector from camera/input.
     */
    void setLookDirection(const glm::vec3& direction);

    /**
     * @brief Set the player's body forward direction
     * @param forward Normalized forward direction of player body
     *
     * Head rotation is relative to body orientation.
     */
    void setBodyForward(const glm::vec3& forward);

    /**
     * @brief Set the player's body up direction
     * @param up Normalized up direction of player body
     */
    void setBodyUp(const glm::vec3& up);

    /**
     * @brief Update head tracking interpolation
     * @param deltaTime Time step in seconds
     */
    void update(float deltaTime);

    /**
     * @brief Get the rotation to apply to head bone
     * @return Quaternion rotation to apply (multiply with animation rotation)
     */
    glm::quat getHeadRotation() const { return m_currentHeadRotation; }

    /**
     * @brief Get the rotation to apply to neck/spine_tip bone
     * @return Quaternion rotation for neck (if neck blending enabled)
     */
    glm::quat getNeckRotation() const { return m_currentNeckRotation; }

    /**
     * @brief Apply head tracking to skeleton animator
     * @param animator The skeleton animator to modify
     */
    void applyToAnimator(SkeletonAnimator& animator);

    /**
     * @brief Enable/disable head tracking
     */
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }

    /**
     * @brief Get/set tracking parameters
     */
    HeadTrackingParams& getParams() { return m_params; }
    const HeadTrackingParams& getParams() const { return m_params; }
    void setParams(const HeadTrackingParams& params) { m_params = params; }

    /**
     * @brief Reset to neutral position
     */
    void reset();

    /**
     * @brief Get current look angles for debugging
     */
    float getCurrentYaw() const { return m_currentYaw; }
    float getCurrentPitch() const { return m_currentPitch; }

private:
    const RuntimeSkeleton* m_skeleton;
    HeadTrackingParams m_params;
    bool m_enabled;

    // Bone indices
    int m_headBoneIndex;
    int m_neckBoneIndex;    // spine_tip

    // Target direction
    glm::vec3 m_lookDirection;
    glm::vec3 m_bodyForward;
    glm::vec3 m_bodyUp;

    // Current interpolated rotation
    float m_currentYaw;      // Current horizontal angle
    float m_currentPitch;    // Current vertical angle
    float m_targetYaw;       // Target horizontal angle
    float m_targetPitch;     // Target vertical angle

    // Output rotations
    glm::quat m_currentHeadRotation;
    glm::quat m_currentNeckRotation;

    /**
     * @brief Calculate target angles from look direction
     */
    void calculateTargetAngles();

    /**
     * @brief Interpolate current angles toward target
     */
    void interpolateAngles(float deltaTime);

    /**
     * @brief Build rotation quaternions from angles
     */
    void buildRotations();
};

} // namespace PlayerPhysics
