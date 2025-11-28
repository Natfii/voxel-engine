/**
 * @file preview_animator.h
 * @brief Procedural animation preview for skeleton rigs
 */

#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>

struct Bone;

/**
 * @brief Animation types for preview
 */
enum class PreviewAnimationType {
    NONE,
    IDLE,       // Subtle breathing motion
    WALK,       // Leg swing animation
    TAIL_WAG    // Tail wagging (if tail present)
};

/**
 * @brief Provides procedural animations for skeleton preview
 */
class PreviewAnimator {
public:
    PreviewAnimator();

    /**
     * @brief Set the bones to animate
     */
    void setBones(const std::vector<Bone>& bones);

    /**
     * @brief Update animation
     * @param deltaTime Frame delta time
     */
    void update(float deltaTime);

    /**
     * @brief Get animated transform for a bone
     * @param boneName Name of the bone
     * @return Local transform matrix (identity if bone not found)
     */
    glm::mat4 getBoneTransform(const std::string& boneName) const;

    /**
     * @brief Set the current animation
     */
    void setAnimation(PreviewAnimationType type);

    /**
     * @brief Get current animation type
     */
    PreviewAnimationType getAnimation() const { return m_currentAnimation; }

    /**
     * @brief Set animation speed multiplier
     */
    void setSpeed(float speed) { m_speed = speed; }

    /**
     * @brief Get animation speed
     */
    float getSpeed() const { return m_speed; }

    /**
     * @brief Reset animation to start
     */
    void reset();

    /**
     * @brief Check if animation is playing
     */
    bool isPlaying() const { return m_isPlaying; }

    /**
     * @brief Set playing state
     */
    void setPlaying(bool playing) { m_isPlaying = playing; }

    /**
     * @brief Check if skeleton has tail bones
     */
    bool hasTail() const { return m_hasTail; }

private:
    void updateIdleAnimation();
    void updateWalkAnimation();
    void updateTailAnimation();

    std::vector<Bone> m_bones;
    std::unordered_map<std::string, glm::mat4> m_transforms;

    PreviewAnimationType m_currentAnimation = PreviewAnimationType::NONE;
    float m_time = 0.0f;
    float m_speed = 1.0f;
    bool m_isPlaying = false;
    bool m_hasTail = false;
};
