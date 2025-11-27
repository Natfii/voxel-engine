/**
 * @file skeletal_editor.cpp
 * @brief Implementation of the 3D skeletal annotation editor
 */

#include "editor/skeletal_editor.h"
#include "editor/rig_file.h"
#include "vulkan_renderer.h"
#include "console_commands.h"
#include "logger.h"

#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>

SkeletalEditor::SkeletalEditor() {
    // Initialize camera with reasonable defaults
    m_camera = EditorCamera();
}

SkeletalEditor::~SkeletalEditor() {
    close();
}

bool SkeletalEditor::initialize(VulkanRenderer* renderer) {
    m_renderer = renderer;
    Logger::info() << "SkeletalEditor initialized";
    return true;
}

void SkeletalEditor::update(float deltaTime) {
    if (!m_isOpen) return;
    // Camera updates handled via processInput
}

void SkeletalEditor::render() {
    if (!m_isOpen) return;

    // Main editor window - no close button in editor-only mode (debug 2)
    // Pass nullptr for p_open to hide the close button entirely
    ImGui::SetNextWindowSize(ImVec2(1200, 800), ImGuiCond_FirstUseEver);

    // Prevent window move when dragging the orb
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar;
    if (m_isDraggingOrb) {
        windowFlags |= ImGuiWindowFlags_NoMove;
    }
    ImGui::Begin("3D Skeletal Editor", ConsoleCommands::isEditorOnlyMode() ? nullptr : &m_isOpen, windowFlags);

    renderMenuBar();
    renderToolbar();

    // Main content area with panels
    float panelWidth = 250.0f;

    // Left panel - Hierarchy
    if (m_showHierarchy) {
        ImGui::BeginChild("HierarchyPanel", ImVec2(panelWidth, 0), true);
        renderHierarchyPanel();
        ImGui::EndChild();
        ImGui::SameLine();
    }

    // Center - Viewport (no scroll bars to not interfere with zoom)
    ImGui::BeginChild("ViewportPanel", ImVec2(-panelWidth - 10, 0), true,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    renderViewport();
    ImGui::EndChild();
    ImGui::SameLine();

    // Right panel - Properties/Wizard
    ImGui::BeginChild("RightPanel", ImVec2(panelWidth, 0), true);
    if (m_showWizard) {
        renderWizardPanel();
        ImGui::Separator();
    }
    if (m_showProperties) {
        renderPropertiesPanel();
    }
    ImGui::EndChild();

    renderStatusBar();

    ImGui::End();

    // File browser dialog
    if (m_fileBrowser.isOpen()) {
        if (m_fileBrowser.render()) {
            std::string selectedPath = m_fileBrowser.getSelectedPath();
            switch (m_browserMode) {
                case BrowserMode::SAVE_RIG:
                    if (RigFile::save(selectedPath, m_state)) {
                        m_currentRigPath = selectedPath;
                        Logger::info() << "Saved rig to: " << selectedPath;
                    }
                    break;
                case BrowserMode::LOAD_RIG:
                    if (RigFile::load(selectedPath, m_state)) {
                        m_currentRigPath = selectedPath;
                        if (!m_state.getModelPath().empty()) {
                            loadModel(m_state.getModelPath());
                        }
                        Logger::info() << "Loaded rig from: " << selectedPath;
                    }
                    break;
                case BrowserMode::LOAD_MODEL:
                    loadModel(selectedPath);
                    break;
                default:
                    break;
            }
            m_browserMode = BrowserMode::NONE;
        }
    }
}

void SkeletalEditor::renderMenuBar() {
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Rig", "Ctrl+N")) {
                newRig();
            }
            if (ImGui::MenuItem("Load Rig...", "Ctrl+O")) {
                m_browserMode = BrowserMode::LOAD_RIG;
                m_fileBrowser.open(FileBrowser::Mode::OPEN, "Load Rig", {".yaml", ".rig"}, "assets/rigs");
            }
            if (ImGui::MenuItem("Save Rig", "Ctrl+S")) {
                if (!m_currentRigPath.empty()) {
                    RigFile::save(m_currentRigPath, m_state);
                } else {
                    m_browserMode = BrowserMode::SAVE_RIG;
                    m_fileBrowser.open(FileBrowser::Mode::SAVE, "Save Rig", {".yaml", ".rig"}, "assets/rigs");
                }
            }
            if (ImGui::MenuItem("Save Rig As...", "Ctrl+Shift+S")) {
                m_browserMode = BrowserMode::SAVE_RIG;
                m_fileBrowser.open(FileBrowser::Mode::SAVE, "Save Rig", {".yaml", ".rig"}, "assets/rigs");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Load Model...", "Ctrl+M")) {
                m_browserMode = BrowserMode::LOAD_MODEL;
                m_fileBrowser.open(FileBrowser::Mode::OPEN, "Load Model", {".glb", ".gltf", ".obj"}, "assets/models");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Close Editor")) {
                close();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, m_state.canUndo())) {
                m_state.undo();
            }
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, m_state.canRedo())) {
                m_state.redo();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Hierarchy Panel", nullptr, &m_showHierarchy);
            ImGui::MenuItem("Properties Panel", nullptr, &m_showProperties);
            ImGui::MenuItem("Wizard Panel", nullptr, &m_showWizard);
            ImGui::Separator();
            if (ImGui::MenuItem("Frame Model", "F")) {
                if (m_hasModel) {
                    m_camera.frameBounds(m_scene.boundsMin, m_scene.boundsMax);
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
}

void SkeletalEditor::renderToolbar() {
    ImGui::BeginChild("Toolbar", ImVec2(0, 35), true);

    if (ImGui::Button("New")) newRig();
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        m_browserMode = BrowserMode::LOAD_RIG;
        m_fileBrowser.open(FileBrowser::Mode::OPEN, "Load Rig", {".yaml", ".rig"}, "assets/rigs");
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        if (!m_currentRigPath.empty()) {
            RigFile::save(m_currentRigPath, m_state);
        } else {
            m_browserMode = BrowserMode::SAVE_RIG;
            m_fileBrowser.open(FileBrowser::Mode::SAVE, "Save Rig", {".yaml", ".rig"}, "assets/rigs");
        }
    }
    ImGui::SameLine();
    ImGui::Separator();
    ImGui::SameLine();

    if (ImGui::Button("Load Model")) {
        m_browserMode = BrowserMode::LOAD_MODEL;
        m_fileBrowser.open(FileBrowser::Mode::OPEN, "Load Model", {".glb", ".gltf", ".obj"}, "assets/models");
    }
    ImGui::SameLine();
    ImGui::Separator();
    ImGui::SameLine();

    // Undo/Redo
    ImGui::BeginDisabled(!m_state.canUndo());
    if (ImGui::Button("Undo")) m_state.undo();
    ImGui::EndDisabled();
    ImGui::SameLine();

    ImGui::BeginDisabled(!m_state.canRedo());
    if (ImGui::Button("Redo")) m_state.redo();
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::Separator();
    ImGui::SameLine();

    // Tail toggle
    bool hasTail = m_state.hasTail();
    if (ImGui::Checkbox("Has Tail", &hasTail)) {
        m_state.setHasTail(hasTail);
    }

    ImGui::SameLine();
    ImGui::Separator();
    ImGui::SameLine();

    // Quick rotate buttons - account for flip state (180 X-rotation swaps front/back and top/bottom)
    // Helper lambda to reset camera target to flipped model center
    auto resetCameraToModel = [this]() {
        glm::vec3 center = (m_scene.boundsMin + m_scene.boundsMax) * 0.5f;
        if (m_modelFlipped) {
            // 180° rotation around X inverts Y and Z
            center.y = -center.y;
            center.z = -center.z;
        }
        m_camera.setTarget(center);
    };

    ImGui::Text("View:");
    ImGui::SameLine();
    if (ImGui::Button("Front")) {
        m_camera.setYaw(0.0f);
        m_camera.setPitch(0.0f);
        resetCameraToModel();
    }
    ImGui::SameLine();
    if (ImGui::Button("Back")) {
        m_camera.setYaw(180.0f);
        m_camera.setPitch(0.0f);
        resetCameraToModel();
    }
    ImGui::SameLine();
    if (ImGui::Button("Left")) {
        m_camera.setYaw(90.0f);
        m_camera.setPitch(0.0f);
        resetCameraToModel();
    }
    ImGui::SameLine();
    if (ImGui::Button("Right")) {
        m_camera.setYaw(-90.0f);
        m_camera.setPitch(0.0f);
        resetCameraToModel();
    }
    ImGui::SameLine();
    if (ImGui::Button("Top")) {
        m_camera.setYaw(0.0f);
        m_camera.setPitch(89.0f);
        resetCameraToModel();
    }
    ImGui::SameLine();
    if (ImGui::Button("Bottom")) {
        m_camera.setYaw(0.0f);
        m_camera.setPitch(-89.0f);
        resetCameraToModel();
    }

    ImGui::SameLine();
    ImGui::Separator();
    ImGui::SameLine();

    // Flip 180 toggle for upside-down models
    if (ImGui::Button(m_modelFlipped ? "Unflip" : "Flip 180")) {
        m_modelFlipped = !m_modelFlipped;
        // Flip the camera pitch and reset target to the flipped model center
        float currentPitch = m_camera.getPitch();
        m_camera.setPitch(-currentPitch);
        // Update camera target to match flipped model position
        glm::vec3 center = (m_scene.boundsMin + m_scene.boundsMax) * 0.5f;
        if (m_modelFlipped) {
            center.y = -center.y;
            center.z = -center.z;
        }
        m_camera.setTarget(center);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Flip model 180 degrees (for upside-down models)\nAlso adjusts camera view");
    }

    ImGui::EndChild();
}

void SkeletalEditor::renderHierarchyPanel() {
    ImGui::Text("Bone Hierarchy");
    ImGui::Separator();

    const auto& bones = m_state.getBones();
    std::string selectedName = m_state.getSelectedBoneName();

    // Build tree structure
    for (const auto& bone : bones) {
        if (bone.parent.empty()) {  // Root bones
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
            if (bone.name == selectedName) {
                flags |= ImGuiTreeNodeFlags_Selected;
            }

            bool opened = ImGui::TreeNodeEx(bone.name.c_str(), flags);
            if (ImGui::IsItemClicked()) {
                m_state.selectBone(bone.name);
            }

            if (opened) {
                // Show children
                for (const auto& child : bones) {
                    if (child.parent == bone.name) {
                        ImGuiTreeNodeFlags childFlags = ImGuiTreeNodeFlags_Leaf;
                        if (child.name == selectedName) {
                            childFlags |= ImGuiTreeNodeFlags_Selected;
                        }
                        ImGui::TreeNodeEx(child.name.c_str(), childFlags);
                        if (ImGui::IsItemClicked()) {
                            m_state.selectBone(child.name);
                        }
                        ImGui::TreePop();
                    }
                }
                ImGui::TreePop();
            }
        }
    }

    if (bones.empty()) {
        ImGui::TextDisabled("No bones placed yet");
        ImGui::TextDisabled("Use the Wizard to place bones");
    }
}

void SkeletalEditor::renderPropertiesPanel() {
    ImGui::Text("Properties");
    ImGui::SameLine(); ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Edit selected bone properties\nSelect bones from the hierarchy or viewport");
    ImGui::Separator();

    Bone* selected = m_state.getSelectedBone();
    if (selected) {
        ImGui::Text("Bone: %s", selected->name.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Name of the currently selected bone");

        ImGui::Text("Parent: %s", selected->parent.empty() ? "(root)" : selected->parent.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Parent bone in the hierarchy\n(root) means this is a top-level bone");

        glm::vec3 pos = selected->position;
        if (ImGui::DragFloat3("Position", glm::value_ptr(pos), 0.01f)) {
            m_state.updateBonePosition(selected->name, pos);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Bone position in 3D space (X, Y, Z)\nDrag to adjust or enter values directly");

        ImGui::Checkbox("Optional", &selected->optional);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Mark bone as optional\nOptional bones can be missing from animations");
    } else {
        ImGui::TextDisabled("No bone selected");
        ImGui::TextDisabled("Click a bone in the hierarchy");
        ImGui::TextDisabled("or viewport to select it");
    }
}

void SkeletalEditor::renderWizardPanel() {
    ImGui::Text("Bone Placement Wizard");
    ImGui::SameLine(); ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Step-by-step guide to place all required bones\nFollow the prompts to build your skeleton rig");
    ImGui::Separator();

    if (m_state.isWizardComplete()) {
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "All bones placed!");

        std::vector<std::string> errors;
        if (m_state.validate(errors)) {
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "Rig is valid - ready to save!");
        } else {
            ImGui::TextColored(ImVec4(0.8f, 0.2f, 0.2f, 1.0f), "Validation errors:");
            for (const auto& err : errors) {
                ImGui::BulletText("%s", err.c_str());
            }
        }

        if (ImGui::Button("Restart Wizard")) {
            m_state.startWizard();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Start over and place all bones again");
    } else {
        std::string currentBone = m_state.getCurrentBoneName();
        int currentIdx = m_state.getCurrentBoneIndex();
        int totalBones = static_cast<int>(SkeletonEditorState::REQUIRED_BONES.size());

        if (m_state.hasTail()) totalBones += 2;

        ImGui::Text("Step %d/%d", currentIdx + 1, totalBones);
        ImGui::ProgressBar(static_cast<float>(currentIdx) / totalBones);

        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Place: %s", currentBone.c_str());

        // Bone placement hints with better descriptions
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
        if (currentBone == "spine_root") {
            ImGui::TextWrapped("Place at the base of the spine (hip/pelvis level). This is the root of the skeleton.");
        } else if (currentBone == "spine_tip") {
            ImGui::TextWrapped("Place at the top of the spine (chest/neck base). The spine will connect from root to tip.");
        } else if (currentBone == "head") {
            ImGui::TextWrapped("Place at the center of the head. This controls head rotation.");
        } else if (currentBone == "leg_left" || currentBone == "leg_right") {
            ImGui::TextWrapped("Place at the hip joint where the leg connects to the body.");
        } else if (currentBone == "arm_left" || currentBone == "arm_right") {
            ImGui::TextWrapped("Place at the shoulder joint where the arm connects to the torso.");
        } else if (currentBone.find("tail") != std::string::npos) {
            ImGui::TextWrapped("Place along the tail. tail_root at base, tail_tip at end.");
        } else {
            ImGui::TextWrapped("Position this bone on the model.");
        }
        ImGui::PopStyleColor();

        ImGui::Separator();

        // XYZ Position draggers with preview
        ImGui::Text("Bone Position:");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drag the sliders or enter values to set bone position\nThe red orb in the viewport shows the preview location");

        // Get current position from state (syncs with orb dragging)
        glm::vec3 manualPos = m_state.getPreviewPosition();
        bool posChanged = false;

        // Individual axis controls with colors
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.5f, 0.1f, 0.1f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.6f, 0.2f, 0.2f, 0.5f));
        ImGui::SetNextItemWidth(80);
        if (ImGui::DragFloat("X", &manualPos.x, 0.01f, -100.0f, 100.0f, "%.2f")) {
            posChanged = true;
        }
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Left/Right position");

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.5f, 0.1f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.2f, 0.6f, 0.2f, 0.5f));
        ImGui::SetNextItemWidth(80);
        if (ImGui::DragFloat("Y", &manualPos.y, 0.01f, -100.0f, 100.0f, "%.2f")) {
            posChanged = true;
        }
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Up/Down position");

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.1f, 0.5f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.2f, 0.2f, 0.6f, 0.5f));
        ImGui::SetNextItemWidth(80);
        if (ImGui::DragFloat("Z", &manualPos.z, 0.01f, -100.0f, 100.0f, "%.2f")) {
            posChanged = true;
        }
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Forward/Back position");

        // Only update state when user manually changes values (not every frame)
        if (posChanged) {
            m_state.setPreviewPosition(manualPos);
        }

        ImGui::Spacing();

        if (ImGui::Button("Place Bone", ImVec2(100, 25))) {
            m_state.placeBone(currentBone, manualPos);
            m_state.nextBone();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Confirm bone placement at the preview position");

        ImGui::SameLine();
        ImGui::BeginDisabled(m_state.getCurrentBoneIndex() == 0);
        if (ImGui::Button("Back", ImVec2(60, 25))) {
            m_state.previousBone();
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Go back to the previous bone");

        ImGui::SameLine();
        if (ImGui::Button("Skip", ImVec2(60, 25))) {
            m_state.nextBone();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Skip this bone (can place later)");

        // Controls help
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("Viewport Controls:");
        ImGui::BulletText("Arrow keys: Pan camera");
        ImGui::BulletText("Right-drag: Orbit camera");
        ImGui::BulletText("Scroll: Zoom in/out");
        ImGui::BulletText("F: Frame model");
    }
}

void SkeletalEditor::renderViewport() {
    ImVec2 size = ImGui::GetContentRegionAvail();
    m_viewportWidth = size.x;
    m_viewportHeight = size.y;

    ImVec2 viewportPos = ImGui::GetCursorScreenPos();

    // Invisible button to capture all mouse input in viewport area
    ImGui::InvisibleButton("viewport_input", size,
        ImGuiButtonFlags_MouseButtonLeft |
        ImGuiButtonFlags_MouseButtonRight |
        ImGuiButtonFlags_MouseButtonMiddle);
    bool viewportHovered = ImGui::IsItemHovered();
    bool viewportActive = ImGui::IsItemActive();

    // Viewport background
    ImGui::GetWindowDrawList()->AddRectFilled(
        viewportPos,
        ImVec2(viewportPos.x + size.x, viewportPos.y + size.y),
        IM_COL32(40, 40, 50, 255)
    );

    // Info text if no model
    if (!m_hasModel) {
        ImVec2 textPos = viewportPos;
        textPos.x += size.x * 0.5f - 100;
        textPos.y += size.y * 0.5f;
        ImGui::GetWindowDrawList()->AddText(textPos, IM_COL32(150, 150, 150, 255),
            "Load a model to begin");
    }

    // Render gizmo
    renderGizmo();

    // === ORB INTERACTION CHECK (MUST BE BEFORE CAMERA CONTROLS) ===
    // Calculate orb screen position for early interaction check
    glm::mat4 earlyView = m_camera.getViewMatrix();
    float earlyAspect = (m_viewportHeight > 0.0f) ? (m_viewportWidth / m_viewportHeight) : 1.0f;
    glm::mat4 earlyProj = m_camera.getProjectionMatrix(earlyAspect);
    glm::mat4 earlyModelTransform = m_modelFlipped ?
        glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)) :
        glm::mat4(1.0f);
    glm::mat4 earlyViewProj = earlyProj * earlyView * earlyModelTransform;

    bool hoveringOrb = false;
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mousePos = io.MousePos;

    // Check if mouse is over the orb
    if (!m_state.isWizardComplete()) {
        glm::vec3 previewPos = m_state.getPreviewPosition();
        glm::vec4 clipPos = earlyViewProj * glm::vec4(previewPos, 1.0f);
        if (clipPos.w > 0.0f) {
            glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;
            float screenX = viewportPos.x + (ndc.x * 0.5f + 0.5f) * m_viewportWidth;
            float screenY = viewportPos.y + (-ndc.y * 0.5f + 0.5f) * m_viewportHeight;
            float distToOrb = glm::length(glm::vec2(mousePos.x - screenX, mousePos.y - screenY));
            hoveringOrb = distToOrb < 25.0f;

            // Store orb screen position for later use
            m_orbScreenPos = glm::vec2(screenX, screenY);
            m_orbDepth = clipPos.w;

            // Handle orb drag start - left click on orb
            if (hoveringOrb && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsUsing()) {
                m_isDraggingOrb = true;
            }
        }
    }

    // Handle ongoing orb drag
    if (m_isDraggingOrb) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            // Convert mouse position to normalized device coordinates
            float ndcX = ((mousePos.x - viewportPos.x) / m_viewportWidth) * 2.0f - 1.0f;
            float ndcY = -(((mousePos.y - viewportPos.y) / m_viewportHeight) * 2.0f - 1.0f);

            // Get current preview position
            glm::vec3 previewPos = m_state.getPreviewPosition();

            // Use view-proj WITHOUT model transform for unprojection
            glm::mat4 viewProjNoModel = earlyProj * earlyView;
            glm::mat4 invViewProj = glm::inverse(viewProjNoModel);

            // Unproject near and far points to create a ray
            glm::vec4 nearPoint = invViewProj * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
            glm::vec4 farPoint = invViewProj * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);

            if (nearPoint.w != 0.0f && farPoint.w != 0.0f) {
                glm::vec3 rayOrigin = glm::vec3(nearPoint) / nearPoint.w;
                glm::vec3 rayEnd = glm::vec3(farPoint) / farPoint.w;
                glm::vec3 rayDir = glm::normalize(rayEnd - rayOrigin);

                // Get camera position and forward direction
                glm::vec3 camPos = m_camera.getPosition();
                glm::vec3 camTarget = m_camera.getTarget();
                glm::vec3 camForward = glm::normalize(camTarget - camPos);

                // Create a plane perpendicular to camera at orb's distance
                float orbDist = glm::dot(previewPos - camPos, camForward);

                // Ray-plane intersection: find t where ray hits the plane
                float denom = glm::dot(rayDir, camForward);
                if (std::abs(denom) > 0.0001f) {
                    float t = orbDist / denom;
                    if (t > 0.0f) {
                        glm::vec3 newPos = rayOrigin + rayDir * t;
                        m_state.setPreviewPosition(newPos);
                    }
                }
            }
        } else {
            m_isDraggingOrb = false;
        }
    }

    // Handle viewport input (camera controls - SKIP LEFT MOUSE if dragging orb)
    if (viewportHovered || viewportActive) {
        // Arrow keys to pan
        float panSpeed = 0.05f;
        if (ImGui::IsKeyDown(ImGuiKey_LeftArrow)) {
            m_camera.updatePan(panSpeed, 0.0f);
        }
        if (ImGui::IsKeyDown(ImGuiKey_RightArrow)) {
            m_camera.updatePan(-panSpeed, 0.0f);
        }
        if (ImGui::IsKeyDown(ImGuiKey_UpArrow)) {
            m_camera.updatePan(0.0f, panSpeed);
        }
        if (ImGui::IsKeyDown(ImGuiKey_DownArrow)) {
            m_camera.updatePan(0.0f, -panSpeed);
        }

        // F key to frame model
        if (ImGui::IsKeyPressed(ImGuiKey_F) && m_hasModel) {
            m_camera.frameBounds(m_scene.boundsMin, m_scene.boundsMax);
        }

        // Scroll to zoom
        if (io.MouseWheel != 0.0f) {
            m_camera.updateZoom(-io.MouseWheel * 0.5f);
        }

        // Right-drag to orbit (not affected by orb)
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
            m_camera.updateOrbit(delta.x * 0.5f, delta.y * 0.5f);
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Right);
        }

        // Middle-drag to pan (not affected by orb)
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle);
            m_camera.updatePan(-delta.x * 0.01f, delta.y * 0.01f);
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
        }
    }

    glm::mat4 view = m_camera.getViewMatrix();

    // Guard against zero viewport dimensions
    float aspectRatio = (m_viewportHeight > 0.0f) ? (m_viewportWidth / m_viewportHeight) : 1.0f;
    glm::mat4 proj = m_camera.getProjectionMatrix(aspectRatio);

    // Apply 180-degree flip transform if enabled
    glm::mat4 modelTransform = glm::mat4(1.0f);
    if (m_modelFlipped) {
        // Rotate 180 degrees around X-axis to flip model upside down
        modelTransform = glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    }

    glm::mat4 viewProj = proj * view * modelTransform;

    // Draw model as wireframe
    if (m_hasModel) {
        ImU32 wireColor = IM_COL32(120, 140, 180, 200);

        // Set up clipping rect for viewport
        ImGui::GetWindowDrawList()->PushClipRect(
            viewportPos,
            ImVec2(viewportPos.x + m_viewportWidth, viewportPos.y + m_viewportHeight),
            true);

        for (const auto& mesh : m_scene.meshes) {
            // Draw wireframe triangles
            for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                const auto& v0 = mesh.vertices[mesh.indices[i]].position;
                const auto& v1 = mesh.vertices[mesh.indices[i + 1]].position;
                const auto& v2 = mesh.vertices[mesh.indices[i + 2]].position;

                // Transform vertices
                glm::vec4 clip0 = viewProj * glm::vec4(v0, 1.0f);
                glm::vec4 clip1 = viewProj * glm::vec4(v1, 1.0f);
                glm::vec4 clip2 = viewProj * glm::vec4(v2, 1.0f);

                // Skip triangles behind camera
                if (clip0.w <= 0.0f || clip1.w <= 0.0f || clip2.w <= 0.0f) continue;

                // Convert to screen space
                auto toScreen = [&](const glm::vec4& clip) -> ImVec2 {
                    glm::vec3 ndc = glm::vec3(clip) / clip.w;
                    return ImVec2(
                        viewportPos.x + (ndc.x * 0.5f + 0.5f) * m_viewportWidth,
                        viewportPos.y + (-ndc.y * 0.5f + 0.5f) * m_viewportHeight
                    );
                };

                ImVec2 s0 = toScreen(clip0);
                ImVec2 s1 = toScreen(clip1);
                ImVec2 s2 = toScreen(clip2);

                // Draw triangle edges
                ImGui::GetWindowDrawList()->AddLine(s0, s1, wireColor, 1.0f);
                ImGui::GetWindowDrawList()->AddLine(s1, s2, wireColor, 1.0f);
                ImGui::GetWindowDrawList()->AddLine(s2, s0, wireColor, 1.0f);
            }
        }

        ImGui::GetWindowDrawList()->PopClipRect();
    }

    // Draw bones as spheres in viewport
    const auto& bones = m_state.getBones();

    for (const auto& bone : bones) {
        glm::vec4 clipPos = viewProj * glm::vec4(bone.position, 1.0f);
        if (clipPos.w > 0.0f) {
            glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;
            float screenX = viewportPos.x + (ndc.x * 0.5f + 0.5f) * m_viewportWidth;
            float screenY = viewportPos.y + (-ndc.y * 0.5f + 0.5f) * m_viewportHeight;

            ImU32 color = (bone.name == m_state.getSelectedBoneName())
                ? IM_COL32(255, 200, 50, 255)
                : IM_COL32(100, 150, 255, 255);

            ImGui::GetWindowDrawList()->AddCircleFilled(
                ImVec2(screenX, screenY), 8.0f, color);
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(screenX + 10, screenY - 5), IM_COL32(255, 255, 255, 200),
                bone.name.c_str());
        }
    }

    // Draw bone connections
    for (const auto& bone : bones) {
        if (!bone.parent.empty()) {
            const Bone* parent = m_state.getBone(bone.parent);
            if (parent) {
                glm::vec4 clipPos1 = viewProj * glm::vec4(bone.position, 1.0f);
                glm::vec4 clipPos2 = viewProj * glm::vec4(parent->position, 1.0f);

                if (clipPos1.w > 0.0f && clipPos2.w > 0.0f) {
                    glm::vec3 ndc1 = glm::vec3(clipPos1) / clipPos1.w;
                    glm::vec3 ndc2 = glm::vec3(clipPos2) / clipPos2.w;

                    float x1 = viewportPos.x + (ndc1.x * 0.5f + 0.5f) * m_viewportWidth;
                    float y1 = viewportPos.y + (-ndc1.y * 0.5f + 0.5f) * m_viewportHeight;
                    float x2 = viewportPos.x + (ndc2.x * 0.5f + 0.5f) * m_viewportWidth;
                    float y2 = viewportPos.y + (-ndc2.y * 0.5f + 0.5f) * m_viewportHeight;

                    ImGui::GetWindowDrawList()->AddLine(
                        ImVec2(x1, y1), ImVec2(x2, y2),
                        IM_COL32(100, 200, 100, 200), 2.0f);
                }
            }
        }
    }

    // Draw preview orb for bone placement (interaction handled earlier in function)
    if (!m_state.isWizardComplete()) {
        // Use stored screen position from earlier interaction check
        float screenX = m_orbScreenPos.x;
        float screenY = m_orbScreenPos.y;

        // Check if orb is visible (depth > 0 means it was calculated)
        if (m_orbDepth > 0.0f) {
            // Recalculate hover for visual feedback
            float distToOrb = glm::length(glm::vec2(mousePos.x - screenX, mousePos.y - screenY));
            bool orbHovered = distToOrb < 25.0f;

            // Pulsing effect (faster pulse when dragging)
            float pulseSpeed = m_isDraggingOrb ? 8.0f : 4.0f;
            float pulse = (sinf(static_cast<float>(ImGui::GetTime()) * pulseSpeed) + 1.0f) * 0.5f;
            float orbSize = m_isDraggingOrb ? 14.0f : (10.0f + pulse * 4.0f);

            // Color changes on hover/drag
            ImU32 orbColor;
            if (m_isDraggingOrb) {
                orbColor = IM_COL32(255, 150, 50, 255);  // Orange when dragging
            } else if (orbHovered) {
                orbColor = IM_COL32(255, 100, 100, 255);  // Brighter red on hover
            } else {
                orbColor = IM_COL32(255, 50, 50, static_cast<int>(180 + pulse * 75));
            }

            // Draw preview orb with glow
            ImGui::GetWindowDrawList()->AddCircleFilled(
                ImVec2(screenX, screenY), orbSize + 4.0f, IM_COL32(255, 100, 100, 50));
            ImGui::GetWindowDrawList()->AddCircleFilled(
                ImVec2(screenX, screenY), orbSize, orbColor);
            ImGui::GetWindowDrawList()->AddCircle(
                ImVec2(screenX, screenY), orbSize + 2.0f, IM_COL32(255, 255, 255, 150), 0, 2.0f);

            // Label with drag hint
            std::string label = "Preview: " + m_state.getCurrentBoneName();
            if (orbHovered && !m_isDraggingOrb) {
                label += " (drag to move)";
            }
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(screenX + 15, screenY - 5), IM_COL32(255, 200, 200, 255),
                label.c_str());

            // Change cursor when hovering orb
            if (orbHovered || m_isDraggingOrb) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            }
        }
    } else {
        m_isDraggingOrb = false;  // Reset drag state when wizard is complete
    }
}

void SkeletalEditor::renderGizmo() {
    Bone* selected = m_state.getSelectedBone();
    if (!selected) return;

    ImGuizmo::BeginFrame();
    ImGuizmo::SetOrthographic(false);

    // Use the content region position, not window position
    // This accounts for window padding and title bar
    ImVec2 viewportPos = ImGui::GetCursorScreenPos();
    ImVec2 viewportSize = ImGui::GetContentRegionAvail();

    // Guard against zero-size viewport
    if (viewportSize.x < 1.0f || viewportSize.y < 1.0f) {
        return;
    }

    ImGuizmo::SetRect(viewportPos.x, viewportPos.y, viewportSize.x, viewportSize.y);

    glm::mat4 view = m_camera.getViewMatrix();

    // Guard against zero viewport dimensions
    float aspectRatio = (m_viewportHeight > 0.0f) ? (m_viewportWidth / m_viewportHeight) : 1.0f;
    glm::mat4 proj = m_camera.getProjectionMatrix(aspectRatio);

    glm::mat4 boneMatrix = glm::translate(glm::mat4(1.0f), selected->position);

    ImGuizmo::Manipulate(
        glm::value_ptr(view),
        glm::value_ptr(proj),
        ImGuizmo::TRANSLATE,
        ImGuizmo::WORLD,
        glm::value_ptr(boneMatrix)
    );

    if (ImGuizmo::IsUsing()) {
        glm::vec3 newPos = glm::vec3(boneMatrix[3]);
        m_state.updateBonePosition(selected->name, newPos);
    }
}

void SkeletalEditor::renderStatusBar() {
    ImGui::Separator();
    ImGui::Text("Model: %s | Rig: %s | Bones: %zu",
        m_currentModelPath.empty() ? "(none)" : m_currentModelPath.c_str(),
        m_currentRigPath.empty() ? "(unsaved)" : m_currentRigPath.c_str(),
        m_state.getBones().size());
}

void SkeletalEditor::open(const std::string& modelPath) {
    m_isOpen = true;
    m_state.startWizard();

    if (!modelPath.empty()) {
        loadModel(modelPath);
    }

    Logger::info() << "Skeletal Editor opened";
}

void SkeletalEditor::close() {
    m_isOpen = false;
    Logger::info() << "Skeletal Editor closed";
}

void SkeletalEditor::processInput(float mouseX, float mouseY, bool leftDown, bool rightDown, float scrollDelta) {
    // Input handled in renderViewport via ImGui
}

void SkeletalEditor::loadModel(const std::string& path) {
    try {
        m_scene = MeshLoader::loadGLTFScene(path);
        m_currentModelPath = path;
        m_state.setModelPath(path);
        m_hasModel = true;

        // Frame the model
        m_camera.frameBounds(m_scene.boundsMin, m_scene.boundsMax);

        Logger::info() << "Loaded model: " << path << " (" << m_scene.meshes.size() << " meshes)";
    } catch (const std::exception& e) {
        Logger::error() << "Failed to load model: " << path << " - " << e.what();
        m_hasModel = false;
    }
}

void SkeletalEditor::saveRig() {
    m_browserMode = BrowserMode::SAVE_RIG;
    m_fileBrowser.open(FileBrowser::Mode::SAVE, "Save Rig", {".yaml", ".rig"}, "assets/rigs");
}

void SkeletalEditor::loadRig() {
    m_browserMode = BrowserMode::LOAD_RIG;
    m_fileBrowser.open(FileBrowser::Mode::OPEN, "Load Rig", {".yaml", ".rig"}, "assets/rigs");
}

void SkeletalEditor::newRig() {
    m_state.clear();
    m_state.startWizard();
    m_currentRigPath.clear();
}
