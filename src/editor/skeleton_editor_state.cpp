#include "editor/skeleton_editor_state.h"
#include <algorithm>

const std::vector<std::string> SkeletonEditorState::REQUIRED_BONES = {
    "spine_root", "spine_tip", "leg_L", "leg_R", "arm_L", "arm_R", "head"
};

SkeletonEditorState::SkeletonEditorState()
    : m_currentBoneIndex(0)
    , m_hasTail(false)
{
}

void SkeletonEditorState::startWizard() {
    clear();
    m_currentBoneIndex = 0;
}

void SkeletonEditorState::nextBone() {
    if (!isWizardComplete()) {
        m_currentBoneIndex++;
    }
}

void SkeletonEditorState::previousBone() {
    if (m_currentBoneIndex > 0) {
        m_currentBoneIndex--;
    }
}

bool SkeletonEditorState::isWizardComplete() const {
    // Wizard is complete when all required bones are placed
    // Plus tail bones if hasTail is true
    int requiredCount = static_cast<int>(REQUIRED_BONES.size());
    if (m_hasTail) {
        requiredCount += 2; // tail_base and tail_tip
    }

    return m_currentBoneIndex >= requiredCount;
}

std::string SkeletonEditorState::getCurrentBoneName() const {
    if (m_currentBoneIndex < static_cast<int>(REQUIRED_BONES.size())) {
        return REQUIRED_BONES[m_currentBoneIndex];
    }

    // Handle tail bones
    if (m_hasTail) {
        int tailIndex = m_currentBoneIndex - static_cast<int>(REQUIRED_BONES.size());
        if (tailIndex == 0) return "tail_base";
        if (tailIndex == 1) return "tail_tip";
    }

    return "";
}

void SkeletonEditorState::placeBone(const std::string& name, const glm::vec3& position) {
    saveStateForUndo();

    Bone bone;
    bone.name = name;
    bone.position = position;
    bone.parent = getParentBone(name);
    bone.optional = (name == "tail_base" || name == "tail_tip");

    m_bones.push_back(bone);
    m_selectedBone = name;
}

void SkeletonEditorState::updateBonePosition(const std::string& name, const glm::vec3& position) {
    saveStateForUndo();

    Bone* bone = getBone(name);
    if (bone) {
        bone->position = position;
    }
}

Bone* SkeletonEditorState::getBone(const std::string& name) {
    auto it = std::find_if(m_bones.begin(), m_bones.end(),
        [&name](const Bone& b) { return b.name == name; });

    return (it != m_bones.end()) ? &(*it) : nullptr;
}

const Bone* SkeletonEditorState::getBone(const std::string& name) const {
    auto it = std::find_if(m_bones.begin(), m_bones.end(),
        [&name](const Bone& b) { return b.name == name; });

    return (it != m_bones.end()) ? &(*it) : nullptr;
}

void SkeletonEditorState::selectBone(const std::string& name) {
    if (getBone(name)) {
        m_selectedBone = name;
    }
}

Bone* SkeletonEditorState::getSelectedBone() {
    return m_selectedBone.empty() ? nullptr : getBone(m_selectedBone);
}

void SkeletonEditorState::setHasTail(bool hasTail) {
    if (m_hasTail != hasTail) {
        saveStateForUndo();
        m_hasTail = hasTail;

        // Remove tail bones if setting to false
        if (!hasTail) {
            m_bones.erase(
                std::remove_if(m_bones.begin(), m_bones.end(),
                    [](const Bone& b) { return b.name == "tail_base" || b.name == "tail_tip"; }),
                m_bones.end()
            );
        }
    }
}

void SkeletonEditorState::undo() {
    if (canUndo()) {
        m_redoStack.push_back(m_bones);
        m_bones = m_undoStack.back();
        m_undoStack.pop_back();

        // Clear selection if selected bone no longer exists
        if (!m_selectedBone.empty() && !getBone(m_selectedBone)) {
            m_selectedBone.clear();
        }
    }
}

void SkeletonEditorState::redo() {
    if (canRedo()) {
        m_undoStack.push_back(m_bones);
        m_bones = m_redoStack.back();
        m_redoStack.pop_back();

        if (m_undoStack.size() > MAX_UNDO_LEVELS) {
            m_undoStack.erase(m_undoStack.begin());
        }
    }
}

bool SkeletonEditorState::validate(std::vector<std::string>& errors) const {
    errors.clear();

    // Check all required bones are present
    for (const auto& boneName : REQUIRED_BONES) {
        if (!getBone(boneName)) {
            errors.push_back("Missing required bone: " + boneName);
        }
    }

    // If hasTail is true, check tail bones are present
    if (m_hasTail) {
        if (!getBone("tail_base")) {
            errors.push_back("Missing tail bone: tail_base");
        }
        if (!getBone("tail_tip")) {
            errors.push_back("Missing tail bone: tail_tip");
        }
    }

    // Validate parent relationships
    for (const auto& bone : m_bones) {
        if (!bone.parent.empty() && !getBone(bone.parent)) {
            errors.push_back("Bone '" + bone.name + "' has invalid parent: " + bone.parent);
        }
    }

    // Check for cycles in parent hierarchy
    for (const auto& bone : m_bones) {
        std::string current = bone.parent;
        std::vector<std::string> visited = { bone.name };

        while (!current.empty()) {
            if (std::find(visited.begin(), visited.end(), current) != visited.end()) {
                errors.push_back("Circular parent dependency detected for bone: " + bone.name);
                break;
            }

            visited.push_back(current);
            const Bone* parentBone = getBone(current);
            current = parentBone ? parentBone->parent : "";
        }
    }

    return errors.empty();
}

void SkeletonEditorState::clear() {
    m_bones.clear();
    m_selectedBone.clear();
    m_currentBoneIndex = 0;
    m_undoStack.clear();
    m_redoStack.clear();
}

void SkeletonEditorState::saveStateForUndo() {
    m_undoStack.push_back(m_bones);
    if (m_undoStack.size() > MAX_UNDO_LEVELS) {
        m_undoStack.erase(m_undoStack.begin());
    }
    m_redoStack.clear();
}

std::string SkeletonEditorState::getParentBone(const std::string& boneName) const {
    // Define parent relationships
    if (boneName == "spine_root") return "";
    if (boneName == "spine_tip") return "spine_root";
    if (boneName == "leg_L") return "spine_root";
    if (boneName == "leg_R") return "spine_root";
    if (boneName == "arm_L") return "spine_tip";
    if (boneName == "arm_R") return "spine_tip";
    if (boneName == "head") return "spine_tip";
    if (boneName == "tail_base") return "spine_root";
    if (boneName == "tail_tip") return "tail_base";

    return "";
}
