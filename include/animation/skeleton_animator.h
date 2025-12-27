/**
 * @file skeleton_animator.h
 * @brief Runtime skeleton animation system for character models
 */

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

/**
 * @brief Runtime bone data loaded from rig file
 */
struct RuntimeBone {
    std::string name;
    glm::vec3 position;         // Local position relative to parent
    glm::quat rotation;         // Local rotation
    glm::vec3 scale;            // Local scale
    int parentIndex;            // Index of parent bone (-1 for root)
    std::vector<int> children;  // Child bone indices

    // Animation state (modified at runtime)
    glm::vec3 animPosition;
    glm::quat animRotation;
    glm::vec3 animScale;

    // Computed transforms
    glm::mat4 localTransform;
    glm::mat4 worldTransform;
    glm::mat4 finalTransform;       // Includes inverse bind pose
    glm::mat4 inverseBindPose;      // Inverse of initial world transform (bind pose)
    bool bindPoseComputed = false;  // Whether inverse bind pose has been calculated
};

/**
 * @brief Skeleton loaded from a rig file
 */
struct RuntimeSkeleton {
    std::string name;
    std::string modelPath;
    std::vector<RuntimeBone> bones;
    std::unordered_map<std::string, int> boneNameToIndex;

    int findBone(const std::string& name) const {
        auto it = boneNameToIndex.find(name);
        return it != boneNameToIndex.end() ? it->second : -1;
    }
};

/**
 * @brief Animation keyframe
 */
struct AnimationKeyframe {
    float time;
    glm::vec3 position;
    glm::quat rotation;
    glm::vec3 scale;
};

/**
 * @brief Animation track for a single bone
 */
struct BoneAnimationTrack {
    std::string boneName;
    int boneIndex;
    std::vector<AnimationKeyframe> keyframes;
};

/**
 * @brief Animation clip containing multiple bone tracks
 */
struct AnimationClip {
    std::string name;
    float duration;
    bool loop;
    std::vector<BoneAnimationTrack> tracks;
};

/**
 * @brief Procedural animation types
 */
enum class ProceduralAnimation {
    IDLE,       // Subtle breathing/swaying
    WALK,       // Walking cycle
    RUN,        // Running cycle
    JUMP,       // Jump animation
    ATTACK,     // Attack swing
    CUSTOM
};

/**
 * @brief Skeleton animator for character models
 *
 * Loads rig files created by the 3D editor and applies animations
 * to update bone transforms for rendering.
 */
class SkeletonAnimator {
public:
    SkeletonAnimator();
    ~SkeletonAnimator();

    /**
     * @brief Load a skeleton from a rig file
     * @param rigPath Path to .rig file
     * @return True on success
     */
    bool loadSkeleton(const std::string& rigPath);

    /**
     * @brief Load an animation clip from file
     * @param clipPath Path to animation file
     * @return True on success
     */
    bool loadAnimation(const std::string& clipPath);

    /**
     * @brief Play an animation clip
     * @param clipName Name of loaded clip
     * @param loop Whether to loop
     * @param blendTime Blend time from current pose (seconds)
     */
    void playAnimation(const std::string& clipName, bool loop = true, float blendTime = 0.2f);

    /**
     * @brief Play a procedural animation
     * @param anim Procedural animation type
     * @param speed Animation speed multiplier
     */
    void playProcedural(ProceduralAnimation anim, float speed = 1.0f);

    /**
     * @brief Stop current animation
     * @param blendToIdle Blend to idle pose
     */
    void stopAnimation(bool blendToIdle = true);

    /**
     * @brief Update animation state
     * @param deltaTime Time since last frame
     */
    void update(float deltaTime);

    /**
     * @brief Get bone world transform for rendering
     * @param boneIndex Bone index
     * @return World transformation matrix
     */
    glm::mat4 getBoneWorldTransform(int boneIndex) const;

    /**
     * @brief Get final bone transform (includes inverse bind pose)
     * @param boneIndex Bone index
     * @return Final transformation matrix for skinning
     */
    glm::mat4 getBoneFinalTransform(int boneIndex) const;

    /**
     * @brief Get all final bone transforms as array
     * @return Vector of final transforms
     */
    const std::vector<glm::mat4>& getAllFinalTransforms() const { return m_finalTransforms; }

    /**
     * @brief Check if skeleton is loaded
     */
    bool hasSkeletonLoaded() const { return m_skeleton != nullptr; }

    /**
     * @brief Get loaded skeleton (const)
     */
    const RuntimeSkeleton* getSkeleton() const { return m_skeleton.get(); }

    /**
     * @brief Get loaded skeleton (non-const for physics modifications)
     */
    RuntimeSkeleton* getSkeleton() { return m_skeleton.get(); }

    /**
     * @brief Get bone count
     */
    size_t getBoneCount() const { return m_skeleton ? m_skeleton->bones.size() : 0; }

    /**
     * @brief Get current animation time
     */
    float getCurrentTime() const { return m_currentTime; }

    /**
     * @brief Check if animation is playing
     */
    bool isPlaying() const { return m_isPlaying; }

    /**
     * @brief Set movement speed for walk/run animations
     * @param speed Speed in units per second
     */
    void setMovementSpeed(float speed) { m_movementSpeed = speed; }

    /**
     * @brief Set character facing direction for animations
     * @param direction Normalized direction vector
     */
    void setFacingDirection(const glm::vec3& direction) { m_facingDirection = direction; }

    /**
     * @brief Recompute bone transforms after external modifications
     *
     * Call this after modifying bone animPosition/animRotation/animScale externally
     * (e.g., from physics system squish deformation) to update the final transforms.
     */
    void recomputeBoneTransforms() { updateBoneTransforms(); }

private:
    void updateBoneTransforms();
    void computeProceduralPose(float time);
    void computeIdlePose(float time);
    void computeWalkPose(float time);
    void computeRunPose(float time);
    void blendPoses(float blendFactor);

    // Interpolation helpers
    glm::vec3 interpolatePosition(const std::vector<AnimationKeyframe>& keyframes, float time);
    glm::quat interpolateRotation(const std::vector<AnimationKeyframe>& keyframes, float time);
    glm::vec3 interpolateScale(const std::vector<AnimationKeyframe>& keyframes, float time);

    std::unique_ptr<RuntimeSkeleton> m_skeleton;
    std::unordered_map<std::string, AnimationClip> m_animations;
    std::vector<glm::mat4> m_finalTransforms;

    // Animation state
    std::string m_currentClip;
    ProceduralAnimation m_proceduralAnim = ProceduralAnimation::IDLE;
    float m_currentTime = 0.0f;
    float m_animSpeed = 1.0f;
    bool m_isPlaying = false;
    bool m_isLooping = false;
    bool m_useProcedural = true;

    // Blending
    float m_blendTime = 0.0f;
    float m_blendDuration = 0.2f;
    std::vector<glm::mat4> m_blendFromPose;

    // Movement input for procedural animations
    float m_movementSpeed = 0.0f;
    glm::vec3 m_facingDirection = glm::vec3(0, 0, -1);
};
