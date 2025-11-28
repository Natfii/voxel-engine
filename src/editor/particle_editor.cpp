/**
 * @file particle_editor.cpp
 * @brief Implementation of the 2D particle editor
 */

#include "editor/particle_editor.h"
#include "editor/particle_effect_file.h"
#include "console_commands.h"
#include "logger.h"

#include <imgui.h>
#include <implot.h>
#include <glm/gtc/type_ptr.hpp>

ParticleEditor::ParticleEditor() {
    m_effect = ParticleEffect::createDefault();
}

ParticleEditor::~ParticleEditor() {
    close();
}

bool ParticleEditor::initialize(VulkanRenderer* renderer) {
    m_renderer = renderer;

    // Create ImPlot context if not already created
    if (ImPlot::GetCurrentContext() == nullptr) {
        ImPlot::CreateContext();
    }

    Logger::info() << "ParticleEditor initialized";
    return true;
}

void ParticleEditor::update(float deltaTime) {
    if (!m_isOpen) return;

    if (m_isPlaying) {
        m_time += deltaTime * m_playbackSpeed;

        // Calculate max duration from all emitters
        float maxDuration = 0.0f;
        for (const auto& emitter : m_effect.emitters) {
            maxDuration = std::max(maxDuration, emitter.duration);
        }
        if (maxDuration < 0.1f) maxDuration = 5.0f;

        // Update all emitters
        for (auto& emitter : m_emitters) {
            emitter->update(deltaTime * m_playbackSpeed);
        }

        // Loop at max duration
        if (m_time >= maxDuration) {
            m_time = 0.0f;
            for (auto& emitter : m_emitters) {
                emitter->reset();
            }
        }
    }
}

void ParticleEditor::render() {
    if (!m_isOpen) return;

    // No close button in editor-only mode (debug 2)
    // Pass nullptr for p_open to hide the close button entirely
    ImGui::SetNextWindowSize(ImVec2(1000, 700), ImGuiCond_FirstUseEver);
    ImGui::Begin("Particle Effect Editor", ConsoleCommands::isEditorOnlyMode() ? nullptr : &m_isOpen, ImGuiWindowFlags_MenuBar);

    renderMenuBar();
    renderToolbar();

    // Main content
    float listWidth = 200.0f;
    float propsWidth = 300.0f;

    // Left - Emitter list
    ImGui::BeginChild("EmitterList", ImVec2(listWidth, -100), true);
    renderEmitterList();
    ImGui::EndChild();
    ImGui::SameLine();

    // Center - Viewport
    ImGui::BeginChild("Viewport", ImVec2(-propsWidth - 10, -100), true);
    renderViewport();
    ImGui::EndChild();
    ImGui::SameLine();

    // Right - Properties
    ImGui::BeginChild("Properties", ImVec2(propsWidth, -100), true);
    renderEmitterProperties();
    ImGui::EndChild();

    // Bottom - Timeline
    ImGui::BeginChild("Timeline", ImVec2(0, 90), true);
    renderTimeline();
    ImGui::EndChild();

    ImGui::End();

    // File browser dialog
    if (m_fileBrowser.isOpen()) {
        if (m_fileBrowser.render()) {
            std::string selectedPath = m_fileBrowser.getSelectedPath();
            switch (m_browserMode) {
                case BrowserMode::SAVE:
                    if (ParticleEffectFile::save(selectedPath, m_effect)) {
                        m_currentPath = selectedPath;
                    }
                    break;
                case BrowserMode::LOAD:
                    if (ParticleEffectFile::load(selectedPath, m_effect)) {
                        m_currentPath = selectedPath;
                        // Recreate emitters from loaded effect
                        m_emitters.clear();
                        for (const auto& config : m_effect.emitters) {
                            m_emitters.push_back(std::make_unique<ParticleEmitter>(config));
                        }
                        m_selectedEmitter = 0;
                        m_time = 0.0f;
                    }
                    break;
                default:
                    break;
            }
            m_browserMode = BrowserMode::NONE;
        }
    }
}

void ParticleEditor::renderMenuBar() {
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Effect", "Ctrl+N")) {
                newEffect();
            }
            if (ImGui::MenuItem("Load...", "Ctrl+O")) {
                m_browserMode = BrowserMode::LOAD;
                m_fileBrowser.open(FileBrowser::Mode::OPEN, "Load Effect", {".yaml", ".particle"}, "assets/particles");
            }
            if (ImGui::MenuItem("Save", "Ctrl+S")) {
                if (!m_currentPath.empty()) {
                    if (!ParticleEffectFile::save(m_currentPath, m_effect)) {
                        Logger::error() << "Failed to save effect, opening Save As dialog";
                        m_browserMode = BrowserMode::SAVE;
                        m_fileBrowser.open(FileBrowser::Mode::SAVE, "Save Effect", {".yaml", ".particle"}, "assets/particles");
                    }
                } else {
                    m_browserMode = BrowserMode::SAVE;
                    m_fileBrowser.open(FileBrowser::Mode::SAVE, "Save Effect", {".yaml", ".particle"}, "assets/particles");
                }
            }
            if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) {
                m_browserMode = BrowserMode::SAVE;
                m_fileBrowser.open(FileBrowser::Mode::SAVE, "Save Effect", {".yaml", ".particle"}, "assets/particles");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Close")) {
                close();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Emitter")) {
            if (ImGui::MenuItem("Add Emitter")) {
                m_effect.emitters.push_back(EmitterConfig());
                m_effect.emitters.back().name = "Emitter " + std::to_string(m_effect.emitters.size());

                auto emitter = std::make_unique<ParticleEmitter>(m_effect.emitters.back());
                m_emitters.push_back(std::move(emitter));
            }
            if (ImGui::MenuItem("Duplicate", nullptr, false, m_selectedEmitter >= 0)) {
                if (m_selectedEmitter >= 0 && m_selectedEmitter < static_cast<int>(m_effect.emitters.size())) {
                    m_effect.emitters.push_back(m_effect.emitters[m_selectedEmitter]);
                    m_effect.emitters.back().name += " (copy)";

                    auto emitter = std::make_unique<ParticleEmitter>(m_effect.emitters.back());
                    m_emitters.push_back(std::move(emitter));
                }
            }
            if (ImGui::MenuItem("Delete", nullptr, false, m_selectedEmitter >= 0 && m_effect.emitters.size() > 1)) {
                if (m_selectedEmitter >= 0 && m_selectedEmitter < static_cast<int>(m_effect.emitters.size())) {
                    m_effect.emitters.erase(m_effect.emitters.begin() + m_selectedEmitter);
                    m_emitters.erase(m_emitters.begin() + m_selectedEmitter);
                    if (m_selectedEmitter >= static_cast<int>(m_effect.emitters.size())) {
                        m_selectedEmitter = static_cast<int>(m_effect.emitters.size()) - 1;
                    }
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Presets")) {
            if (ImGui::MenuItem("Fire")) {
                applyPreset("fire");
            }
            if (ImGui::MenuItem("Smoke")) {
                applyPreset("smoke");
            }
            if (ImGui::MenuItem("Sparkle")) {
                applyPreset("sparkle");
            }
            if (ImGui::MenuItem("Explosion")) {
                applyPreset("explosion");
            }
            if (ImGui::MenuItem("Rain")) {
                applyPreset("rain");
            }
            if (ImGui::MenuItem("Snow")) {
                applyPreset("snow");
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
}

void ParticleEditor::renderToolbar() {
    ImGui::BeginChild("Toolbar", ImVec2(0, 35), true);

    // Playback controls
    if (ImGui::Button(m_isPlaying ? "Pause" : "Play")) {
        m_isPlaying = !m_isPlaying;
    }
    ImGui::SameLine();
    if (ImGui::Button("Restart")) {
        m_time = 0.0f;
        for (auto& emitter : m_emitters) {
            emitter->reset();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Burst")) {
        for (auto& emitter : m_emitters) {
            emitter->burst(10);
        }
    }

    ImGui::SameLine();
    ImGui::Separator();
    ImGui::SameLine();

    ImGui::SetNextItemWidth(100);
    ImGui::SliderFloat("Speed", &m_playbackSpeed, 0.1f, 3.0f, "%.1fx");

    ImGui::SameLine();
    ImGui::Text("Time: %.2fs", m_time);

    ImGui::SameLine();
    size_t totalParticles = 0;
    for (const auto& emitter : m_emitters) {
        totalParticles += emitter->getActiveCount();
    }
    ImGui::Text("Particles: %zu", totalParticles);

    ImGui::EndChild();
}

void ParticleEditor::renderEmitterList() {
    ImGui::Text("Emitters");
    ImGui::Separator();

    for (int i = 0; i < static_cast<int>(m_effect.emitters.size()); ++i) {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (i == m_selectedEmitter) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }

        ImGui::TreeNodeEx(m_effect.emitters[i].name.c_str(), flags);
        if (ImGui::IsItemClicked()) {
            m_selectedEmitter = i;
        }
    }

    ImGui::Separator();
    if (ImGui::Button("+ Add Emitter")) {
        m_effect.emitters.push_back(EmitterConfig());
        m_effect.emitters.back().name = "Emitter " + std::to_string(m_effect.emitters.size());

        auto emitter = std::make_unique<ParticleEmitter>(m_effect.emitters.back());
        m_emitters.push_back(std::move(emitter));

        // Select the newly added emitter
        m_selectedEmitter = static_cast<int>(m_emitters.size()) - 1;
    }
}

void ParticleEditor::renderEmitterProperties() {
    ImGui::Text("Properties");
    ImGui::Separator();

    if (m_selectedEmitter < 0 || m_selectedEmitter >= static_cast<int>(m_effect.emitters.size())) {
        ImGui::TextDisabled("Select an emitter");
        return;
    }

    EmitterConfig& config = m_effect.emitters[m_selectedEmitter];
    bool configChanged = false;

    // Track if any widget is actively being edited to defer config updates
    bool anyWidgetActive = false;

    // Name
    char nameBuffer[64];
    strncpy(nameBuffer, config.name.c_str(), sizeof(nameBuffer));
    if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer))) {
        config.name = nameBuffer;
    }

    ImGui::Separator();
    ImGui::Text("Emission");
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Controls where and how particles spawn");
    }

    // Shape
    const char* shapes[] = {"Point", "Cone", "Box", "Circle"};
    int shapeIdx = static_cast<int>(config.shape);
    if (ImGui::Combo("Spawn Shape", &shapeIdx, shapes, 4)) {
        config.shape = static_cast<EmitterShape>(shapeIdx);
        configChanged = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Where particles spawn from:\n- Point: Single point\n- Cone: Cone direction\n- Box: Random in volume\n- Circle: Random on circle");
    }

    // Shape-specific
    if (config.shape == EmitterShape::CONE) {
        configChanged |= ImGui::SliderFloat("Cone Angle", &config.coneAngle, 0.0f, 180.0f);
        anyWidgetActive |= ImGui::IsItemActive();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Spread angle of the cone");
    } else if (config.shape == EmitterShape::BOX) {
        configChanged |= ImGui::DragFloat3("Box Size", glm::value_ptr(config.boxSize), 0.1f, 0.0f, 100.0f);
        anyWidgetActive |= ImGui::IsItemActive();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Size of spawn area (X, Y, Z)");
    } else if (config.shape == EmitterShape::CIRCLE) {
        configChanged |= ImGui::DragFloat("Circle Radius", &config.circleRadius, 0.1f, 0.0f, 100.0f);
        anyWidgetActive |= ImGui::IsItemActive();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Radius of spawn circle");
    }

    // Rate
    configChanged |= ImGui::DragFloatRange2("Rate", &config.rate.min, &config.rate.max, 1.0f, 0.0f, 1000.0f, "%.0f", "%.0f");
    anyWidgetActive |= ImGui::IsItemActive();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Particles spawned per second (min-max range)");

    // Duration & Loop
    configChanged |= ImGui::DragFloat("Duration", &config.duration, 0.1f, 0.0f, 60.0f, "%.1f sec");
    anyWidgetActive |= ImGui::IsItemActive();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("How long the emitter runs before stopping/looping");
    configChanged |= ImGui::Checkbox("Loop", &config.loop);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Restart emitter when duration ends");

    ImGui::Separator();
    ImGui::Text("Particle");
    ImGui::SameLine(); ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Controls individual particle behavior");

    // Lifetime
    configChanged |= ImGui::DragFloatRange2("Lifetime", &config.lifetime.min, &config.lifetime.max, 0.1f, 0.0f, 30.0f, "%.1f", "%.1f");
    anyWidgetActive |= ImGui::IsItemActive();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("How long each particle lives (seconds)\nRandom value between min and max");

    // Speed
    configChanged |= ImGui::DragFloatRange2("Speed", &config.speed.min, &config.speed.max, 0.1f, 0.0f, 100.0f);
    anyWidgetActive |= ImGui::IsItemActive();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Initial velocity of particles\nHigher = faster movement");

    // Angle
    configChanged |= ImGui::DragFloatRange2("Angle", &config.angle.min, &config.angle.max, 1.0f, 0.0f, 360.0f, "%.0f°", "%.0f°");
    anyWidgetActive |= ImGui::IsItemActive();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Direction particles travel (degrees)\n0° = right, 90° = up, 180° = left, 270° = down");

    ImGui::Separator();
    ImGui::Text("Physics");
    ImGui::SameLine(); ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Forces affecting particle movement");

    // Gravity
    configChanged |= ImGui::DragFloat2("Gravity", glm::value_ptr(config.gravity), 0.1f, -100.0f, 100.0f);
    anyWidgetActive |= ImGui::IsItemActive();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Constant force applied to particles (X, Y)\nPositive Y = up, Negative Y = down");

    configChanged |= ImGui::DragFloat("Drag", &config.drag, 0.01f, 0.0f, 10.0f);
    anyWidgetActive |= ImGui::IsItemActive();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Air resistance - slows particles over time\n0 = no drag, higher = more slowdown");

    ImGui::Separator();
    ImGui::Text("Size");
    ImGui::SameLine(); ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Particle size changes over lifetime");

    configChanged |= ImGui::DragFloat2("Start Size", glm::value_ptr(config.sizeStart), 0.1f, 0.0f, 100.0f);
    anyWidgetActive |= ImGui::IsItemActive();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Size when particle spawns (width, height)");

    configChanged |= ImGui::DragFloat2("End Size", glm::value_ptr(config.sizeEnd), 0.1f, 0.0f, 100.0f);
    anyWidgetActive |= ImGui::IsItemActive();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Size when particle dies (width, height)\nSet to 0 for shrinking effect");

    ImGui::Separator();
    ImGui::Text("Color");
    ImGui::SameLine(); ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Particle color fades from start to end");

    // Clear gradient once if it has data (so colorStart/colorEnd are used instead)
    if (!config.colorGradient.empty()) {
        config.colorGradient.clear();
    }

    // Start color
    float startCol[4] = {config.colorStart.r, config.colorStart.g, config.colorStart.b, config.colorStart.a};
    ImGui::PushID("StartColorPicker");
    if (ImGui::ColorEdit4("Start Color", startCol,
        ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreview)) {
        config.colorStart = glm::vec4(startCol[0], startCol[1], startCol[2], startCol[3]);
    }
    anyWidgetActive |= ImGui::IsItemActive();
    ImGui::PopID();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Color when particle spawns\nAlpha controls transparency");

    // End color
    float endCol[4] = {config.colorEnd.r, config.colorEnd.g, config.colorEnd.b, config.colorEnd.a};
    ImGui::PushID("EndColorPicker");
    if (ImGui::ColorEdit4("End Color", endCol,
        ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreview)) {
        config.colorEnd = glm::vec4(endCol[0], endCol[1], endCol[2], endCol[3]);
    }
    anyWidgetActive |= ImGui::IsItemActive();
    ImGui::PopID();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Color when particle dies\nSet alpha to 0 for fade-out effect");

    ImGui::Separator();
    ImGui::Text("Rendering");
    ImGui::SameLine(); ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("How particles are drawn");

    // Render shape
    const char* renderShapes[] = {"Circle", "Square", "Triangle", "Star", "Ring", "Spark"};
    int renderShapeIdx = static_cast<int>(config.renderShape);
    if (ImGui::Combo("Particle Shape", &renderShapeIdx, renderShapes, 6)) {
        config.renderShape = static_cast<ParticleRenderShape>(renderShapeIdx);
        configChanged = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Visual shape of each particle:\n- Circle: Round particles\n- Square: Box particles\n- Triangle: Arrow-like\n- Star: Sparkle effect\n- Ring: Hollow circle\n- Spark: Elongated streak");

    configChanged |= ImGui::Checkbox("Align to Velocity", &config.alignToVelocity);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rotate particles to face movement direction\nGood for rain, sparks, projectiles");

    const char* blendModes[] = {"Alpha", "Additive", "Premultiplied"};
    int blendIdx = static_cast<int>(config.texture.blend);
    if (ImGui::Combo("Blend Mode", &blendIdx, blendModes, 3)) {
        config.texture.blend = static_cast<ParticleBlendMode>(blendIdx);
        configChanged = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("How particles blend with background:\n- Alpha: Normal transparency\n- Additive: Glowing/bright (fire, sparks)\n- Premultiplied: Smooth edges");

    // Only update emitter config when user is NOT actively dragging a widget
    // This prevents rapid updates and potential crashes during editing
    if (m_selectedEmitter >= 0 && m_selectedEmitter < static_cast<int>(m_emitters.size())) {
        if (!anyWidgetActive) {
            m_emitters[m_selectedEmitter]->setConfig(config);
        }
        // Reset emitter when major config changes (shape, etc) - but not during active editing
        if (configChanged && !anyWidgetActive) {
            m_emitters[m_selectedEmitter]->reset();
        }
    }
}

void ParticleEditor::renderViewport() {
    ImVec2 size = ImGui::GetContentRegionAvail();
    ImVec2 pos = ImGui::GetCursorScreenPos();

    // Background
    ImGui::GetWindowDrawList()->AddRectFilled(
        pos,
        ImVec2(pos.x + size.x, pos.y + size.y),
        ImGui::ColorConvertFloat4ToU32(ImVec4(
            m_backgroundColor.r, m_backgroundColor.g, m_backgroundColor.b, m_backgroundColor.a))
    );

    // Grid
    float gridSize = 50.0f * m_zoom;
    ImU32 gridColor = IM_COL32(60, 60, 70, 100);
    float centerX = pos.x + size.x * 0.5f + m_viewOffset.x;
    float centerY = pos.y + size.y * 0.5f + m_viewOffset.y;

    // Draw grid lines
    for (float x = centerX; x < pos.x + size.x; x += gridSize) {
        ImGui::GetWindowDrawList()->AddLine(ImVec2(x, pos.y), ImVec2(x, pos.y + size.y), gridColor);
    }
    for (float x = centerX; x > pos.x; x -= gridSize) {
        ImGui::GetWindowDrawList()->AddLine(ImVec2(x, pos.y), ImVec2(x, pos.y + size.y), gridColor);
    }
    for (float y = centerY; y < pos.y + size.y; y += gridSize) {
        ImGui::GetWindowDrawList()->AddLine(ImVec2(pos.x, y), ImVec2(pos.x + size.x, y), gridColor);
    }
    for (float y = centerY; y > pos.y; y -= gridSize) {
        ImGui::GetWindowDrawList()->AddLine(ImVec2(pos.x, y), ImVec2(pos.x + size.x, y), gridColor);
    }

    // Center crosshair
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(centerX - 10, centerY), ImVec2(centerX + 10, centerY),
        IM_COL32(100, 100, 120, 200), 2.0f);
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(centerX, centerY - 10), ImVec2(centerX, centerY + 10),
        IM_COL32(100, 100, 120, 200), 2.0f);

    // Draw particles
    for (size_t emitterIdx = 0; emitterIdx < m_emitters.size(); ++emitterIdx) {
        const auto& emitter = m_emitters[emitterIdx];
        const EmitterConfig& config = (emitterIdx < m_effect.emitters.size())
            ? m_effect.emitters[emitterIdx]
            : m_effect.emitters[0];

        for (const auto& particle : emitter->getParticles()) {
            if (!particle.isAlive()) continue;

            float screenX = centerX + particle.position.x * m_zoom * 10.0f;
            float screenY = centerY - particle.position.y * m_zoom * 10.0f;  // Y-up in world

            // Skip if outside viewport
            if (screenX < pos.x || screenX > pos.x + size.x ||
                screenY < pos.y || screenY > pos.y + size.y) continue;

            float renderSize = particle.size.x * m_zoom * 5.0f;

            // Clamp color values to valid range to prevent overflow/crashes
            float r = glm::clamp(particle.color.r, 0.0f, 1.0f);
            float g = glm::clamp(particle.color.g, 0.0f, 1.0f);
            float b = glm::clamp(particle.color.b, 0.0f, 1.0f);
            float a = glm::clamp(particle.color.a, 0.0f, 1.0f);
            ImU32 color = ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a));

            ImVec2 center(screenX, screenY);
            ImDrawList* drawList = ImGui::GetWindowDrawList();

            // Draw based on render shape
            switch (config.renderShape) {
                case ParticleRenderShape::CIRCLE:
                    drawList->AddCircleFilled(center, renderSize, color);
                    break;

                case ParticleRenderShape::SQUARE:
                    drawList->AddRectFilled(
                        ImVec2(screenX - renderSize, screenY - renderSize),
                        ImVec2(screenX + renderSize, screenY + renderSize),
                        color);
                    break;

                case ParticleRenderShape::TRIANGLE: {
                    float h = renderSize * 1.5f;
                    drawList->AddTriangleFilled(
                        ImVec2(screenX, screenY - h * 0.66f),
                        ImVec2(screenX - renderSize, screenY + h * 0.33f),
                        ImVec2(screenX + renderSize, screenY + h * 0.33f),
                        color);
                    break;
                }

                case ParticleRenderShape::STAR: {
                    // 5-pointed star
                    const int points = 5;
                    float outerR = renderSize;
                    float innerR = renderSize * 0.4f;
                    for (int i = 0; i < points; ++i) {
                        float angle1 = (i * 2 * 3.14159f / points) - 3.14159f / 2;
                        float angle2 = ((i * 2 + 1) * 3.14159f / points) - 3.14159f / 2;
                        float angle3 = ((i * 2 + 2) * 3.14159f / points) - 3.14159f / 2;
                        ImVec2 p1(screenX + cosf(angle1) * outerR, screenY + sinf(angle1) * outerR);
                        ImVec2 p2(screenX + cosf(angle2) * innerR, screenY + sinf(angle2) * innerR);
                        ImVec2 p3(screenX + cosf(angle3) * outerR, screenY + sinf(angle3) * outerR);
                        drawList->AddTriangleFilled(center, p1, p2, color);
                        drawList->AddTriangleFilled(center, p2, p3, color);
                    }
                    break;
                }

                case ParticleRenderShape::RING:
                    drawList->AddCircle(center, renderSize, color, 0, 2.0f);
                    break;

                case ParticleRenderShape::SPARK: {
                    // Elongated spark shape
                    float len = renderSize * 2.0f;
                    drawList->AddLine(
                        ImVec2(screenX - len * 0.5f, screenY),
                        ImVec2(screenX + len * 0.5f, screenY),
                        color, renderSize * 0.3f);
                    drawList->AddCircleFilled(center, renderSize * 0.4f, color);
                    break;
                }
            }
        }
    }

    // Handle viewport input
    if (ImGui::IsWindowHovered()) {
        ImGuiIO& io = ImGui::GetIO();

        // Zoom
        if (io.MouseWheel != 0.0f) {
            m_zoom *= (1.0f + io.MouseWheel * 0.1f);
            m_zoom = glm::clamp(m_zoom, 0.1f, 10.0f);
        }

        // Pan
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle);
            m_viewOffset.x += delta.x;
            m_viewOffset.y += delta.y;
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
        }
    }
}

void ParticleEditor::renderTimeline() {
    // Calculate max duration
    float maxDuration = 0.0f;
    for (const auto& emitter : m_effect.emitters) {
        maxDuration = std::max(maxDuration, emitter.duration);
    }
    if (maxDuration < 0.1f) maxDuration = 5.0f;

    // Header row
    ImGui::Text("Timeline");
    ImGui::SameLine();
    ImGui::TextDisabled("(%.2f / %.2f sec)", m_time, maxDuration);
    ImGui::SameLine(ImGui::GetWindowWidth() - 200);

    // Zoom control
    ImGui::SetNextItemWidth(80);
    ImGui::SliderFloat("##Zoom", &m_zoom, 0.1f, 10.0f, "%.1fx");
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        m_zoom = 1.0f;
        m_viewOffset = glm::vec2(0.0f);
    }

    // Progress bar
    float progress = maxDuration > 0.0f ? m_time / maxDuration : 0.0f;
    ImGui::ProgressBar(progress, ImVec2(-1, 8), "");

    // Time scrubber
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##Time", &m_time, 0.0f, maxDuration, "")) {
        // Seek all emitters
        for (auto& emitter : m_emitters) {
            emitter->reset();
        }
    }

    // Show emitter durations as colored bars
    ImVec2 barPos = ImGui::GetCursorScreenPos();
    float barWidth = ImGui::GetContentRegionAvail().x;
    float barHeight = 20.0f;

    for (size_t i = 0; i < m_effect.emitters.size(); ++i) {
        const auto& config = m_effect.emitters[i];
        float emitterWidth = (config.duration / maxDuration) * barWidth;

        ImU32 barColor = (static_cast<int>(i) == m_selectedEmitter)
            ? IM_COL32(100, 150, 255, 200)
            : IM_COL32(80, 100, 140, 150);

        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(barPos.x, barPos.y + i * 4),
            ImVec2(barPos.x + emitterWidth, barPos.y + i * 4 + 3),
            barColor);
    }

    // Playhead
    float playheadX = barPos.x + progress * barWidth;
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(playheadX, barPos.y - 5),
        ImVec2(playheadX, barPos.y + m_effect.emitters.size() * 4 + 5),
        IM_COL32(255, 200, 50, 255), 2.0f);
}

void ParticleEditor::open(const std::string& effectPath) {
    m_isOpen = true;
    m_isPlaying = true;
    m_time = 0.0f;

    if (!effectPath.empty()) {
        // Load effect from file
        if (ParticleEffectFile::load(effectPath, m_effect)) {
            m_currentPath = effectPath;
            // Recreate emitters from loaded effect
            m_emitters.clear();
            for (const auto& config : m_effect.emitters) {
                m_emitters.push_back(std::make_unique<ParticleEmitter>(config));
            }
            // Handle empty emitter array edge case
            m_selectedEmitter = m_emitters.empty() ? -1 : 0;
            Logger::info() << "Loaded effect with " << m_emitters.size() << " emitters";
        } else {
            Logger::error() << "Failed to load effect: " << effectPath;
            newEffect();
        }
    } else {
        newEffect();
    }

    Logger::info() << "Particle Editor opened";
}

void ParticleEditor::close() {
    m_isOpen = false;
    Logger::info() << "Particle Editor closed";
}

void ParticleEditor::newEffect() {
    m_effect = ParticleEffect::createDefault();
    m_emitters.clear();

    for (const auto& config : m_effect.emitters) {
        m_emitters.push_back(std::make_unique<ParticleEmitter>(config));
    }

    m_selectedEmitter = 0;
    m_time = 0.0f;
    m_currentPath.clear();
}

void ParticleEditor::saveEffect() {
    m_browserMode = BrowserMode::SAVE;
    m_fileBrowser.open(FileBrowser::Mode::SAVE, "Save Effect", {".yaml", ".particle"}, "assets/particles");
}

void ParticleEditor::loadEffect() {
    m_browserMode = BrowserMode::LOAD;
    m_fileBrowser.open(FileBrowser::Mode::OPEN, "Load Effect", {".yaml", ".particle"}, "assets/particles");
}

void ParticleEditor::applyPreset(const std::string& presetName) {
    m_effect.emitters.clear();
    m_emitters.clear();

    EmitterConfig config;

    if (presetName == "fire") {
        config.name = "Fire";
        config.shape = EmitterShape::CONE;
        config.coneAngle = 25.0f;
        config.renderShape = ParticleRenderShape::CIRCLE;
        config.rate = {30.0f, 50.0f};
        config.lifetime = {0.5f, 1.5f};
        config.speed = {2.0f, 5.0f};
        config.angle = {70.0f, 110.0f};
        config.gravity = {0.0f, 2.0f};
        config.sizeStart = {1.5f, 1.5f};
        config.sizeEnd = {0.2f, 0.2f};
        config.colorStart = glm::vec4(1.0f, 0.6f, 0.1f, 1.0f);
        config.colorEnd = glm::vec4(1.0f, 0.1f, 0.0f, 0.0f);
        config.duration = 3.0f;
    }
    else if (presetName == "smoke") {
        config.name = "Smoke";
        config.shape = EmitterShape::CIRCLE;
        config.circleRadius = 0.3f;
        config.renderShape = ParticleRenderShape::CIRCLE;
        config.rate = {10.0f, 20.0f};
        config.lifetime = {2.0f, 4.0f};
        config.speed = {0.5f, 1.5f};
        config.angle = {70.0f, 110.0f};
        config.gravity = {0.0f, 1.0f};
        config.drag = 0.5f;
        config.sizeStart = {1.0f, 1.0f};
        config.sizeEnd = {3.0f, 3.0f};
        config.colorStart = glm::vec4(0.3f, 0.3f, 0.3f, 0.6f);
        config.colorEnd = glm::vec4(0.5f, 0.5f, 0.5f, 0.0f);
        config.duration = 5.0f;
    }
    else if (presetName == "sparkle") {
        config.name = "Sparkle";
        config.shape = EmitterShape::POINT;
        config.renderShape = ParticleRenderShape::STAR;
        config.rate = {20.0f, 40.0f};
        config.lifetime = {0.3f, 0.8f};
        config.speed = {3.0f, 8.0f};
        config.angle = {0.0f, 360.0f};
        config.gravity = {0.0f, 0.0f};
        config.sizeStart = {0.8f, 0.8f};
        config.sizeEnd = {0.0f, 0.0f};
        config.colorStart = glm::vec4(1.0f, 1.0f, 0.5f, 1.0f);
        config.colorEnd = glm::vec4(1.0f, 0.8f, 0.2f, 0.0f);
        config.duration = 2.0f;
    }
    else if (presetName == "explosion") {
        config.name = "Explosion";
        config.shape = EmitterShape::POINT;
        config.renderShape = ParticleRenderShape::CIRCLE;
        config.rate = {0.0f, 0.0f};
        config.burst.count = 100;
        config.burst.cycles = 1;
        config.lifetime = {0.5f, 1.5f};
        config.speed = {5.0f, 15.0f};
        config.angle = {0.0f, 360.0f};
        config.gravity = {0.0f, -5.0f};
        config.drag = 2.0f;
        config.sizeStart = {1.5f, 1.5f};
        config.sizeEnd = {0.3f, 0.3f};
        config.colorStart = glm::vec4(1.0f, 0.8f, 0.2f, 1.0f);
        config.colorEnd = glm::vec4(0.8f, 0.2f, 0.0f, 0.0f);
        config.duration = 2.0f;
        config.loop = false;
    }
    else if (presetName == "rain") {
        config.name = "Rain";
        config.shape = EmitterShape::BOX;
        config.boxSize = {20.0f, 0.1f, 1.0f};
        config.renderShape = ParticleRenderShape::SPARK;
        config.rate = {50.0f, 100.0f};
        config.lifetime = {1.0f, 2.0f};
        config.speed = {8.0f, 12.0f};
        config.angle = {260.0f, 280.0f};
        config.gravity = {0.0f, -15.0f};
        config.sizeStart = {0.3f, 0.3f};
        config.sizeEnd = {0.2f, 0.2f};
        config.colorStart = glm::vec4(0.6f, 0.7f, 0.9f, 0.8f);
        config.colorEnd = glm::vec4(0.5f, 0.6f, 0.8f, 0.3f);
        config.alignToVelocity = true;
        config.duration = 5.0f;
    }
    else if (presetName == "snow") {
        config.name = "Snow";
        config.shape = EmitterShape::BOX;
        config.boxSize = {15.0f, 0.1f, 1.0f};
        config.renderShape = ParticleRenderShape::CIRCLE;
        config.rate = {20.0f, 40.0f};
        config.lifetime = {3.0f, 6.0f};
        config.speed = {0.5f, 2.0f};
        config.angle = {250.0f, 290.0f};
        config.gravity = {0.0f, -2.0f};
        config.drag = 1.0f;
        config.sizeStart = {0.4f, 0.4f};
        config.sizeEnd = {0.3f, 0.3f};
        config.colorStart = glm::vec4(1.0f, 1.0f, 1.0f, 0.9f);
        config.colorEnd = glm::vec4(0.9f, 0.95f, 1.0f, 0.0f);
        config.duration = 8.0f;
    }
    else {
        // Default
        config.name = "Default";
        config.duration = 3.0f;
    }

    m_effect.name = presetName + " Effect";
    m_effect.emitters.push_back(config);
    m_emitters.push_back(std::make_unique<ParticleEmitter>(config));
    m_selectedEmitter = 0;
    m_time = 0.0f;
    m_currentPath.clear();

    Logger::info() << "Applied preset: " << presetName;
}
