/**
 * @file skeleton_animator.cpp
 * @brief Runtime skeleton animation implementation
 */

#define GLM_ENABLE_EXPERIMENTAL
#include "animation/skeleton_animator.h"
#include "logger.h"
#include <yaml-cpp/yaml.h>
#include <fstream>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>

SkeletonAnimator::SkeletonAnimator() = default;
SkeletonAnimator::~SkeletonAnimator() = default;

bool SkeletonAnimator::loadSkeleton(const std::string& rigPath) {
    try {
        YAML::Node root = YAML::LoadFile(rigPath);

        m_skeleton = std::make_unique<RuntimeSkeleton>();

        // Support both editor format (model) and legacy format (model_path, name)
        if (root["model"]) {
            m_skeleton->modelPath = root["model"].as<std::string>("");
            // Derive name from model path
            std::string modelPath = m_skeleton->modelPath;
            size_t lastSlash = modelPath.find_last_of("/\\");
            size_t lastDot = modelPath.find_last_of('.');
            if (lastSlash != std::string::npos && lastDot != std::string::npos) {
                m_skeleton->name = modelPath.substr(lastSlash + 1, lastDot - lastSlash - 1);
            } else {
                m_skeleton->name = "unnamed";
            }
        } else {
            m_skeleton->name = root["name"].as<std::string>("unnamed");
            m_skeleton->modelPath = root["model_path"].as<std::string>("");
        }

        // Temporary storage for parent names (editor format uses names, not indices)
        std::vector<std::string> parentNames;

        // Load bones
        if (root["bones"]) {
            for (const auto& boneNode : root["bones"]) {
                RuntimeBone bone;
                bone.name = boneNode["name"].as<std::string>("bone");

                // Position - support both array [x,y,z] and object {x:, y:, z:} formats
                if (boneNode["position"]) {
                    const YAML::Node& posNode = boneNode["position"];
                    if (posNode.IsSequence() && posNode.size() >= 3) {
                        // Editor format: [x, y, z]
                        bone.position.x = posNode[0].as<float>(0.0f);
                        bone.position.y = posNode[1].as<float>(0.0f);
                        bone.position.z = posNode[2].as<float>(0.0f);
                    } else {
                        // Legacy format: {x:, y:, z:}
                        bone.position.x = posNode["x"].as<float>(0.0f);
                        bone.position.y = posNode["y"].as<float>(0.0f);
                        bone.position.z = posNode["z"].as<float>(0.0f);
                    }
                }

                // Rotation (stored as euler angles in degrees) - legacy format only
                if (boneNode["rotation"]) {
                    float pitch = glm::radians(boneNode["rotation"]["pitch"].as<float>(0.0f));
                    float yaw = glm::radians(boneNode["rotation"]["yaw"].as<float>(0.0f));
                    float roll = glm::radians(boneNode["rotation"]["roll"].as<float>(0.0f));
                    bone.rotation = glm::quat(glm::vec3(pitch, yaw, roll));
                } else {
                    bone.rotation = glm::quat(1, 0, 0, 0);
                }

                // Scale - legacy format only
                if (boneNode["scale"]) {
                    bone.scale.x = boneNode["scale"]["x"].as<float>(1.0f);
                    bone.scale.y = boneNode["scale"]["y"].as<float>(1.0f);
                    bone.scale.z = boneNode["scale"]["z"].as<float>(1.0f);
                } else {
                    bone.scale = glm::vec3(1.0f);
                }

                // Parent - support both name (editor) and index (legacy) formats
                bone.parentIndex = -1;  // Default to root
                std::string parentName;
                if (boneNode["parent"]) {
                    if (boneNode["parent"].IsScalar()) {
                        // Could be int or string
                        try {
                            bone.parentIndex = boneNode["parent"].as<int>(-1);
                        } catch (...) {
                            // It's a string (editor format)
                            parentName = boneNode["parent"].as<std::string>("");
                        }
                    }
                }
                parentNames.push_back(parentName);

                // Initialize animation state to bind pose
                bone.animPosition = bone.position;
                bone.animRotation = bone.rotation;
                bone.animScale = bone.scale;

                int boneIndex = static_cast<int>(m_skeleton->bones.size());
                m_skeleton->boneNameToIndex[bone.name] = boneIndex;
                m_skeleton->bones.push_back(bone);
            }

            // Resolve parent names to indices (editor format)
            for (size_t i = 0; i < m_skeleton->bones.size(); ++i) {
                if (!parentNames[i].empty()) {
                    int parentIdx = m_skeleton->findBone(parentNames[i]);
                    m_skeleton->bones[i].parentIndex = parentIdx;
                }
            }

            // Build child relationships
            for (size_t i = 0; i < m_skeleton->bones.size(); ++i) {
                int parentIdx = m_skeleton->bones[i].parentIndex;
                if (parentIdx >= 0 && parentIdx < static_cast<int>(m_skeleton->bones.size())) {
                    m_skeleton->bones[parentIdx].children.push_back(static_cast<int>(i));
                }
            }
        }

        // Initialize final transforms array
        m_finalTransforms.resize(m_skeleton->bones.size(), glm::mat4(1.0f));

        // Compute initial transforms
        updateBoneTransforms();

        Logger::info() << "Loaded skeleton: " << m_skeleton->name
                      << " with " << m_skeleton->bones.size() << " bones";
        return true;

    } catch (const std::exception& e) {
        Logger::error() << "Failed to load skeleton " << rigPath << ": " << e.what();
        m_skeleton.reset();
        return false;
    }
}

bool SkeletonAnimator::loadAnimation(const std::string& clipPath) {
    try {
        YAML::Node root = YAML::LoadFile(clipPath);

        AnimationClip clip;
        clip.name = root["name"].as<std::string>("unnamed");
        clip.duration = root["duration"].as<float>(1.0f);
        clip.loop = root["loop"].as<bool>(false);

        if (root["tracks"]) {
            for (const auto& trackNode : root["tracks"]) {
                BoneAnimationTrack track;
                track.boneName = trackNode["bone"].as<std::string>();

                // Find bone index
                if (m_skeleton) {
                    track.boneIndex = m_skeleton->findBone(track.boneName);
                } else {
                    track.boneIndex = -1;
                }

                // Load keyframes
                if (trackNode["keyframes"]) {
                    for (const auto& kfNode : trackNode["keyframes"]) {
                        AnimationKeyframe kf;
                        kf.time = kfNode["time"].as<float>(0.0f);

                        if (kfNode["position"]) {
                            kf.position.x = kfNode["position"]["x"].as<float>(0.0f);
                            kf.position.y = kfNode["position"]["y"].as<float>(0.0f);
                            kf.position.z = kfNode["position"]["z"].as<float>(0.0f);
                        }

                        if (kfNode["rotation"]) {
                            float pitch = glm::radians(kfNode["rotation"]["pitch"].as<float>(0.0f));
                            float yaw = glm::radians(kfNode["rotation"]["yaw"].as<float>(0.0f));
                            float roll = glm::radians(kfNode["rotation"]["roll"].as<float>(0.0f));
                            kf.rotation = glm::quat(glm::vec3(pitch, yaw, roll));
                        } else {
                            kf.rotation = glm::quat(1, 0, 0, 0);
                        }

                        if (kfNode["scale"]) {
                            kf.scale.x = kfNode["scale"]["x"].as<float>(1.0f);
                            kf.scale.y = kfNode["scale"]["y"].as<float>(1.0f);
                            kf.scale.z = kfNode["scale"]["z"].as<float>(1.0f);
                        } else {
                            kf.scale = glm::vec3(1.0f);
                        }

                        track.keyframes.push_back(kf);
                    }
                }

                clip.tracks.push_back(track);
            }
        }

        m_animations[clip.name] = clip;
        Logger::info() << "Loaded animation: " << clip.name << " (" << clip.duration << "s)";
        return true;

    } catch (const std::exception& e) {
        Logger::error() << "Failed to load animation " << clipPath << ": " << e.what();
        return false;
    }
}

void SkeletonAnimator::playAnimation(const std::string& clipName, bool loop, float blendTime) {
    auto it = m_animations.find(clipName);
    if (it == m_animations.end()) {
        Logger::warning() << "Animation not found: " << clipName;
        return;
    }

    // Store current pose for blending
    m_blendFromPose = m_finalTransforms;
    m_blendDuration = blendTime;
    m_blendTime = 0.0f;

    m_currentClip = clipName;
    m_currentTime = 0.0f;
    m_isPlaying = true;
    m_isLooping = loop;
    m_useProcedural = false;
}

void SkeletonAnimator::playProcedural(ProceduralAnimation anim, float speed) {
    m_proceduralAnim = anim;
    m_animSpeed = speed;
    m_isPlaying = true;
    m_useProcedural = true;
}

void SkeletonAnimator::stopAnimation(bool blendToIdle) {
    if (blendToIdle) {
        m_blendFromPose = m_finalTransforms;
        m_blendDuration = 0.2f;
        m_blendTime = 0.0f;
        m_proceduralAnim = ProceduralAnimation::IDLE;
        m_useProcedural = true;
    } else {
        m_isPlaying = false;
    }
}

void SkeletonAnimator::update(float deltaTime) {
    if (!m_skeleton || !m_isPlaying) return;

    m_currentTime += deltaTime * m_animSpeed;

    if (m_useProcedural) {
        computeProceduralPose(m_currentTime);
    } else {
        // Keyframe animation
        auto it = m_animations.find(m_currentClip);
        if (it != m_animations.end()) {
            const AnimationClip& clip = it->second;

            // Handle looping
            if (m_isLooping) {
                while (m_currentTime >= clip.duration) {
                    m_currentTime -= clip.duration;
                }
            } else if (m_currentTime >= clip.duration) {
                m_currentTime = clip.duration;
                m_isPlaying = false;
            }

            // Apply animation tracks
            for (const auto& track : clip.tracks) {
                if (track.boneIndex < 0 || track.boneIndex >= static_cast<int>(m_skeleton->bones.size())) {
                    continue;
                }

                RuntimeBone& bone = m_skeleton->bones[track.boneIndex];

                if (!track.keyframes.empty()) {
                    bone.animPosition = interpolatePosition(track.keyframes, m_currentTime);
                    bone.animRotation = interpolateRotation(track.keyframes, m_currentTime);
                    bone.animScale = interpolateScale(track.keyframes, m_currentTime);
                }
            }
        }
    }

    // Handle pose blending
    if (m_blendTime < m_blendDuration && !m_blendFromPose.empty()) {
        m_blendTime += deltaTime;
        float blendFactor = glm::clamp(m_blendTime / m_blendDuration, 0.0f, 1.0f);
        blendPoses(blendFactor);
    }

    updateBoneTransforms();
}

void SkeletonAnimator::updateBoneTransforms() {
    if (!m_skeleton) return;

    // Compute local transforms
    for (auto& bone : m_skeleton->bones) {
        glm::mat4 T = glm::translate(glm::mat4(1.0f), bone.animPosition);
        glm::mat4 R = glm::toMat4(bone.animRotation);
        glm::mat4 S = glm::scale(glm::mat4(1.0f), bone.animScale);
        bone.localTransform = T * R * S;
    }

    // Compute world transforms (traverse hierarchy)
    for (size_t i = 0; i < m_skeleton->bones.size(); ++i) {
        RuntimeBone& bone = m_skeleton->bones[i];

        if (bone.parentIndex >= 0) {
            bone.worldTransform = m_skeleton->bones[bone.parentIndex].worldTransform * bone.localTransform;
        } else {
            bone.worldTransform = bone.localTransform;
        }

        // Calculate inverse bind pose on first frame (rest pose)
        if (!bone.bindPoseComputed) {
            bone.inverseBindPose = glm::inverse(bone.worldTransform);
            bone.bindPoseComputed = true;
        }

        // Final transform: current world transform * inverse bind pose
        // This transforms vertices from bind pose space to current animated space
        bone.finalTransform = bone.worldTransform * bone.inverseBindPose;
        m_finalTransforms[i] = bone.finalTransform;
    }
}

void SkeletonAnimator::computeProceduralPose(float time) {
    switch (m_proceduralAnim) {
        case ProceduralAnimation::IDLE:
            computeIdlePose(time);
            break;
        case ProceduralAnimation::WALK:
            computeWalkPose(time);
            break;
        case ProceduralAnimation::RUN:
            computeRunPose(time);
            break;
        default:
            computeIdlePose(time);
            break;
    }
}

void SkeletonAnimator::computeIdlePose(float time) {
    if (!m_skeleton) return;

    // ========== INFLATABLE T-REX COSTUME WOBBLE ==========
    // Multiple sine waves at different frequencies for organic, bouncy motion
    // Like an air-filled costume constantly shifting and wobbling

    // Primary breathing/sloshing motion (slow, big)
    float breathMain = std::sin(time * 1.3f) * 0.025f;
    float breathSecondary = std::sin(time * 2.1f + 0.5f) * 0.012f;

    // Side-to-side sway (makes it look inflated and unstable)
    float sideSwayBody = std::sin(time * 1.7f) * 0.015f;
    float sideSwayHead = std::sin(time * 2.3f + 1.0f) * 0.02f;

    // Subtle forward-back wobble
    float forwardWobble = std::sin(time * 1.9f + 0.3f) * 0.01f;

    // Bouncy scale jiggle (like the costume is full of air)
    float scaleJiggle = std::sin(time * 2.5f) * 0.015f;

    // Leg bounce (like standing on an air mattress)
    float legBounce = std::abs(std::sin(time * 1.8f)) * 0.01f;

    for (size_t i = 0; i < m_skeleton->bones.size(); ++i) {
        auto& bone = m_skeleton->bones[i];

        // Phase offset per bone for asynchronous wobble (more natural)
        float phase = static_cast<float>(i) * 0.2f;

        bone.animPosition = bone.position;
        bone.animRotation = bone.rotation;
        bone.animScale = bone.scale;

        // Apply wobble based on bone type
        if (bone.name.find("spine") != std::string::npos) {
            // Spine gets breathing and sway
            bone.animPosition.y += breathMain + breathSecondary;
            bone.animPosition.x += sideSwayBody * std::sin(phase);
            bone.animPosition.z += forwardWobble;

            // Slight scale pulse (inflating/deflating)
            bone.animScale += glm::vec3(scaleJiggle * 0.5f);

            // Subtle rotation wobble
            float rollWobble = std::sin(time * 2.0f + phase) * glm::radians(1.0f);
            bone.animRotation = bone.rotation * glm::angleAxis(rollWobble, glm::vec3(0, 0, 1));
        }
        else if (bone.name == "head") {
            // Head wobbles independently (like a bobblehead)
            bone.animPosition.x += sideSwayHead;
            bone.animPosition.z += forwardWobble * 1.5f;

            // Head tilt wobble
            float headTilt = std::sin(time * 2.2f) * glm::radians(2.0f);
            float headNod = std::sin(time * 1.8f + 0.5f) * glm::radians(1.5f);
            bone.animRotation = bone.rotation *
                glm::angleAxis(headTilt, glm::vec3(0, 0, 1)) *
                glm::angleAxis(headNod, glm::vec3(1, 0, 0));
        }
        else if (bone.name.find("leg") != std::string::npos) {
            // Legs bounce and sway
            bone.animPosition.y += legBounce;

            // Slight leg sway
            float legSway = std::sin(time * 2.0f + phase) * glm::radians(1.5f);
            bone.animRotation = bone.rotation * glm::angleAxis(legSway, glm::vec3(0, 0, 1));
        }
        else if (bone.name.find("arm") != std::string::npos) {
            // Arms sway loosely
            float armSway = std::sin(time * 1.6f + phase) * glm::radians(3.0f);
            float armBob = std::sin(time * 2.4f + phase) * 0.01f;
            bone.animPosition.y += armBob;
            bone.animRotation = bone.rotation * glm::angleAxis(armSway, glm::vec3(0, 0, 1));
        }
        else if (bone.name.find("tail") != std::string::npos) {
            // Tail wags and wobbles (very bouncy!)
            float tailWag = std::sin(time * 3.0f + phase * 2.0f) * glm::radians(8.0f);
            float tailBob = std::sin(time * 2.5f) * glm::radians(4.0f);
            bone.animRotation = bone.rotation *
                glm::angleAxis(tailWag, glm::vec3(0, 1, 0)) *
                glm::angleAxis(tailBob, glm::vec3(1, 0, 0));
        }
    }
}

void SkeletonAnimator::computeWalkPose(float time) {
    if (!m_skeleton) return;

    float walkCycle = time * 4.0f;  // Walking speed
    float legSwing = std::sin(walkCycle) * 0.6f;   // Increased from 0.4 to 0.6
    float armSwing = std::sin(walkCycle) * 0.45f;  // Increased from 0.3 to 0.45
    float hipSway = std::sin(walkCycle * 2.0f) * 0.03f;

    for (auto& bone : m_skeleton->bones) {
        bone.animPosition = bone.position;
        bone.animRotation = bone.rotation;
        bone.animScale = bone.scale;

        // Leg animation - support both naming conventions
        if (bone.name == "leg_L" || bone.name.find("leg") != std::string::npos && bone.name.find("left") != std::string::npos) {
            bone.animRotation = bone.rotation * glm::angleAxis(legSwing, glm::vec3(1, 0, 0));
        } else if (bone.name == "leg_R" || bone.name.find("leg") != std::string::npos && bone.name.find("right") != std::string::npos) {
            bone.animRotation = bone.rotation * glm::angleAxis(-legSwing, glm::vec3(1, 0, 0));
        }

        // Arm swing (opposite to legs) - support both naming conventions
        if (bone.name == "arm_L" || bone.name.find("arm") != std::string::npos && bone.name.find("left") != std::string::npos) {
            bone.animRotation = bone.rotation * glm::angleAxis(-armSwing, glm::vec3(1, 0, 0));
        } else if (bone.name == "arm_R" || bone.name.find("arm") != std::string::npos && bone.name.find("right") != std::string::npos) {
            bone.animRotation = bone.rotation * glm::angleAxis(armSwing, glm::vec3(1, 0, 0));
        }

        // Hip/spine root sway
        if (bone.name == "spine_root" || bone.name.find("hip") != std::string::npos ||
            bone.name.find("pelvis") != std::string::npos) {
            bone.animPosition.y += hipSway;
        }
    }
}

void SkeletonAnimator::computeRunPose(float time) {
    if (!m_skeleton) return;

    float runCycle = time * 8.0f;  // Running speed (faster than walk)
    float legSwing = std::sin(runCycle) * 0.9f;   // Increased from 0.7 to 0.9
    float armSwing = std::sin(runCycle) * 0.65f;  // Increased from 0.5 to 0.65
    float bounce = std::abs(std::sin(runCycle)) * 0.07f;

    for (auto& bone : m_skeleton->bones) {
        bone.animPosition = bone.position;
        bone.animRotation = bone.rotation;
        bone.animScale = bone.scale;

        // Leg animation - support both naming conventions
        if (bone.name == "leg_L" || bone.name.find("leg") != std::string::npos && bone.name.find("left") != std::string::npos) {
            bone.animRotation = bone.rotation * glm::angleAxis(legSwing, glm::vec3(1, 0, 0));
        } else if (bone.name == "leg_R" || bone.name.find("leg") != std::string::npos && bone.name.find("right") != std::string::npos) {
            bone.animRotation = bone.rotation * glm::angleAxis(-legSwing, glm::vec3(1, 0, 0));
        }

        // Arm swing - support both naming conventions
        if (bone.name == "arm_L" || bone.name.find("arm") != std::string::npos && bone.name.find("left") != std::string::npos) {
            bone.animRotation = bone.rotation * glm::angleAxis(-armSwing, glm::vec3(1, 0, 0));
        } else if (bone.name == "arm_R" || bone.name.find("arm") != std::string::npos && bone.name.find("right") != std::string::npos) {
            bone.animRotation = bone.rotation * glm::angleAxis(armSwing, glm::vec3(1, 0, 0));
        }

        // Body bounce
        if (bone.name == "spine_root" || bone.name.find("hip") != std::string::npos ||
            bone.name.find("root") != std::string::npos) {
            bone.animPosition.y += bounce;
        }
    }
}

void SkeletonAnimator::blendPoses(float blendFactor) {
    if (!m_skeleton || m_blendFromPose.size() != m_finalTransforms.size()) return;

    // Blend each transform
    for (size_t i = 0; i < m_finalTransforms.size(); ++i) {
        // Simple matrix interpolation (for more accurate blending, decompose to TRS)
        m_finalTransforms[i] = m_blendFromPose[i] * (1.0f - blendFactor) +
                               m_finalTransforms[i] * blendFactor;
    }
}

glm::vec3 SkeletonAnimator::interpolatePosition(const std::vector<AnimationKeyframe>& keyframes, float time) {
    if (keyframes.empty()) return glm::vec3(0);
    if (keyframes.size() == 1) return keyframes[0].position;

    // Find surrounding keyframes
    size_t i = 0;
    while (i < keyframes.size() - 1 && keyframes[i + 1].time <= time) {
        ++i;
    }

    if (i >= keyframes.size() - 1) {
        return keyframes.back().position;
    }

    const AnimationKeyframe& a = keyframes[i];
    const AnimationKeyframe& b = keyframes[i + 1];

    float timeDiff = b.time - a.time;
    if (timeDiff < 0.0001f) return a.position;

    float t = (time - a.time) / timeDiff;
    return glm::mix(a.position, b.position, t);
}

glm::quat SkeletonAnimator::interpolateRotation(const std::vector<AnimationKeyframe>& keyframes, float time) {
    if (keyframes.empty()) return glm::quat(1, 0, 0, 0);
    if (keyframes.size() == 1) return keyframes[0].rotation;

    size_t i = 0;
    while (i < keyframes.size() - 1 && keyframes[i + 1].time <= time) {
        ++i;
    }

    if (i >= keyframes.size() - 1) {
        return keyframes.back().rotation;
    }

    const AnimationKeyframe& a = keyframes[i];
    const AnimationKeyframe& b = keyframes[i + 1];

    float timeDiff = b.time - a.time;
    if (timeDiff < 0.0001f) return a.rotation;

    float t = (time - a.time) / timeDiff;
    return glm::slerp(a.rotation, b.rotation, t);
}

glm::vec3 SkeletonAnimator::interpolateScale(const std::vector<AnimationKeyframe>& keyframes, float time) {
    if (keyframes.empty()) return glm::vec3(1);
    if (keyframes.size() == 1) return keyframes[0].scale;

    size_t i = 0;
    while (i < keyframes.size() - 1 && keyframes[i + 1].time <= time) {
        ++i;
    }

    if (i >= keyframes.size() - 1) {
        return keyframes.back().scale;
    }

    const AnimationKeyframe& a = keyframes[i];
    const AnimationKeyframe& b = keyframes[i + 1];

    float timeDiff = b.time - a.time;
    if (timeDiff < 0.0001f) return a.scale;

    float t = (time - a.time) / timeDiff;
    return glm::mix(a.scale, b.scale, t);
}

glm::mat4 SkeletonAnimator::getBoneWorldTransform(int boneIndex) const {
    if (!m_skeleton || boneIndex < 0 || boneIndex >= static_cast<int>(m_skeleton->bones.size())) {
        return glm::mat4(1.0f);
    }
    return m_skeleton->bones[boneIndex].worldTransform;
}

glm::mat4 SkeletonAnimator::getBoneFinalTransform(int boneIndex) const {
    if (boneIndex < 0 || boneIndex >= static_cast<int>(m_finalTransforms.size())) {
        return glm::mat4(1.0f);
    }
    return m_finalTransforms[boneIndex];
}
