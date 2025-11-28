/**
 * @file preview_animator.cpp
 * @brief Implementation of procedural animation preview
 */

#include "editor/preview_animator.h"
#include "editor/skeleton_editor_state.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

PreviewAnimator::PreviewAnimator() {
    reset();
}

void PreviewAnimator::setBones(const std::vector<Bone>& bones) {
    m_bones = bones;
    m_transforms.clear();

    // Check for tail bones and initialize all transforms
    m_hasTail = false;
    for (const auto& bone : bones) {
        // Initialize identity transform for every bone
        m_transforms[bone.name] = glm::mat4(1.0f);

        // Check if this is a tail bone
        if (bone.name == "tail_base" || bone.name == "tail_tip") {
            m_hasTail = true;
        }
    }
}

void PreviewAnimator::update(float deltaTime) {
    if (!m_isPlaying) return;

    m_time += deltaTime * m_speed;

    // Reset all transforms to identity
    for (auto& pair : m_transforms) {
        pair.second = glm::mat4(1.0f);
    }

    switch (m_currentAnimation) {
        case PreviewAnimationType::IDLE:
            updateIdleAnimation();
            break;
        case PreviewAnimationType::WALK:
            updateWalkAnimation();
            break;
        case PreviewAnimationType::TAIL_WAG:
            updateTailAnimation();
            break;
        case PreviewAnimationType::NONE:
        default:
            break;
    }
}

glm::mat4 PreviewAnimator::getBoneTransform(const std::string& boneName) const {
    auto it = m_transforms.find(boneName);
    if (it != m_transforms.end()) {
        return it->second;
    }
    return glm::mat4(1.0f);
}

void PreviewAnimator::setAnimation(PreviewAnimationType type) {
    if (m_currentAnimation != type) {
        m_currentAnimation = type;
        m_time = 0.0f;
    }
}

void PreviewAnimator::reset() {
    m_time = 0.0f;
    m_isPlaying = false;
    m_currentAnimation = PreviewAnimationType::NONE;

    for (auto& pair : m_transforms) {
        pair.second = glm::mat4(1.0f);
    }
}

void PreviewAnimator::updateIdleAnimation() {
    // Subtle breathing motion on spine_tip
    float breathFreq = 1.0f;  // 1 Hz
    float breathAmp = 0.02f;  // 2cm movement

    float breathOffset = std::sin(m_time * 2.0f * glm::pi<float>() * breathFreq) * breathAmp;

    // Apply to spine_tip
    if (m_transforms.find("spine_tip") != m_transforms.end()) {
        m_transforms["spine_tip"] = glm::translate(glm::mat4(1.0f),
            glm::vec3(0.0f, breathOffset, 0.0f));
    }

    // Slight head movement
    if (m_transforms.find("head") != m_transforms.end()) {
        float headBob = std::sin(m_time * 2.0f * glm::pi<float>() * breathFreq * 0.5f) * 0.01f;
        m_transforms["head"] = glm::translate(glm::mat4(1.0f),
            glm::vec3(0.0f, breathOffset + headBob, 0.0f));
    }
}

void PreviewAnimator::updateWalkAnimation() {
    float walkFreq = 1.0f;  // 1 full cycle per second
    float legSwing = 30.0f; // degrees
    float armSwing = 20.0f; // degrees

    float phase = m_time * 2.0f * glm::pi<float>() * walkFreq;

    // Leg swing (opposite legs in sync)
    float legAngleL = std::sin(phase) * glm::radians(legSwing);
    float legAngleR = std::sin(phase + glm::pi<float>()) * glm::radians(legSwing);

    if (m_transforms.find("leg_L") != m_transforms.end()) {
        m_transforms["leg_L"] = glm::rotate(glm::mat4(1.0f), legAngleL,
            glm::vec3(1.0f, 0.0f, 0.0f));  // Rotate around X axis
    }
    if (m_transforms.find("leg_R") != m_transforms.end()) {
        m_transforms["leg_R"] = glm::rotate(glm::mat4(1.0f), legAngleR,
            glm::vec3(1.0f, 0.0f, 0.0f));
    }

    // Arm swing (opposite to legs)
    float armAngleL = std::sin(phase + glm::pi<float>()) * glm::radians(armSwing);
    float armAngleR = std::sin(phase) * glm::radians(armSwing);

    if (m_transforms.find("arm_L") != m_transforms.end()) {
        m_transforms["arm_L"] = glm::rotate(glm::mat4(1.0f), armAngleL,
            glm::vec3(1.0f, 0.0f, 0.0f));
    }
    if (m_transforms.find("arm_R") != m_transforms.end()) {
        m_transforms["arm_R"] = glm::rotate(glm::mat4(1.0f), armAngleR,
            glm::vec3(1.0f, 0.0f, 0.0f));
    }

    // Subtle body bob
    float bobAmp = 0.03f;
    float bob = std::abs(std::sin(phase * 2.0f)) * bobAmp;
    if (m_transforms.find("spine_root") != m_transforms.end()) {
        m_transforms["spine_root"] = glm::translate(glm::mat4(1.0f),
            glm::vec3(0.0f, bob, 0.0f));
    }

    // Also update tail if present
    if (m_hasTail) {
        updateTailAnimation();
    }
}

void PreviewAnimator::updateTailAnimation() {
    if (!m_hasTail) return;

    // Tail base: slower, smaller sway
    float tailBaseFreq = 0.5f;
    float tailBaseAmp = 15.0f;  // degrees

    // Tail tip: faster, larger sway
    float tailTipFreq = 0.8f;
    float tailTipAmp = 20.0f;

    float tailBaseAngle = std::sin(m_time * 2.0f * glm::pi<float>() * tailBaseFreq)
        * glm::radians(tailBaseAmp);
    float tailTipAngle = std::sin(m_time * 2.0f * glm::pi<float>() * tailTipFreq)
        * glm::radians(tailTipAmp);

    if (m_transforms.find("tail_base") != m_transforms.end()) {
        m_transforms["tail_base"] = glm::rotate(glm::mat4(1.0f), tailBaseAngle,
            glm::vec3(0.0f, 1.0f, 0.0f));  // Rotate around Y axis
    }
    if (m_transforms.find("tail_tip") != m_transforms.end()) {
        // Tail tip rotates relative to tail base
        m_transforms["tail_tip"] = glm::rotate(glm::mat4(1.0f), tailTipAngle,
            glm::vec3(0.0f, 1.0f, 0.0f));
    }
}
