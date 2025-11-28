#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>

/**
 * @brief A single bone in the skeleton
 */
struct Bone {
    std::string name;
    std::string parent;  // Empty string if root
    glm::vec3 position = glm::vec3(0.0f);
    bool optional = false;
};

/**
 * @brief Manages the skeleton editing state with undo/redo
 */
class SkeletonEditorState {
public:
    // Required bone names in placement order
    static const std::vector<std::string> REQUIRED_BONES;

    SkeletonEditorState();

    // Wizard control
    void startWizard();
    void nextBone();
    void previousBone();
    void completeWizard();  // Mark wizard as complete (e.g., after loading rig)
    bool isWizardComplete() const;
    int getCurrentBoneIndex() const { return m_currentBoneIndex; }
    std::string getCurrentBoneName() const;

    // Bone manipulation
    void placeBone(const std::string& name, const glm::vec3& position);
    void updateBonePosition(const std::string& name, const glm::vec3& position);
    Bone* getBone(const std::string& name);
    const Bone* getBone(const std::string& name) const;

    // Selection
    void selectBone(const std::string& name);
    Bone* getSelectedBone();
    const std::string& getSelectedBoneName() const { return m_selectedBone; }

    // Tail handling
    void setHasTail(bool hasTail);
    bool hasTail() const { return m_hasTail; }

    // Undo/Redo
    void undo();
    void redo();
    bool canUndo() const { return !m_undoStack.empty(); }
    bool canRedo() const { return !m_redoStack.empty(); }

    // Access all bones
    const std::vector<Bone>& getBones() const { return m_bones; }

    // Validation
    bool validate(std::vector<std::string>& errors) const;

    // Reset
    void clear();

    // Model path tracking
    void setModelPath(const std::string& path) { m_modelPath = path; }
    const std::string& getModelPath() const { return m_modelPath; }

    // Preview position for bone placement
    void setPreviewPosition(const glm::vec3& pos) { m_previewPosition = pos; }
    const glm::vec3& getPreviewPosition() const { return m_previewPosition; }

private:
    void saveStateForUndo();
    std::string getParentBone(const std::string& boneName) const;

    std::vector<Bone> m_bones;
    std::string m_selectedBone;
    int m_currentBoneIndex = 0;
    bool m_hasTail = false;
    std::string m_modelPath;
    glm::vec3 m_previewPosition = glm::vec3(0.0f);

    // Undo/redo stacks
    std::vector<std::vector<Bone>> m_undoStack;
    std::vector<std::vector<Bone>> m_redoStack;
    static const size_t MAX_UNDO_LEVELS = 50;
};
