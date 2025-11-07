#include "inventory.h"
#include "block_system.h"
#include "vulkan_renderer.h"
#include "input_manager.h"
// BlockIconRenderer is now part of block_system.h
#include <algorithm>
#include <imgui.h>

Inventory::Inventory()
    : m_isOpen(false)
    , m_selectedHotbarSlot(0)
    , m_inventoryScrollOffset(0.0f)
{
    // Initialize hotbar with first 10 blocks (air + first solid blocks)
    m_hotbar.resize(HOTBAR_SLOTS);
    auto& registry = BlockRegistry::instance();
    for (int i = 0; i < HOTBAR_SLOTS && i < registry.count(); i++) {
        m_hotbar[i] = i;
    }

    // Initialize available blocks from registry
    for (int i = 0; i < registry.count(); i++) {
        m_availableBlocks.push_back(i);
    }

    // Clear search buffer
    m_searchBuffer[0] = '\0';
}

void Inventory::update(GLFWwindow* window, float deltaTime) {
    if (!m_isOpen) {
        handleHotbarInput(window);
    }
}

void Inventory::render(VulkanRenderer* renderer) {
    if (m_isOpen) {
        renderInventoryGrid(renderer);
    }
}

void Inventory::renderHotbar(VulkanRenderer* renderer) {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 displaySize = io.DisplaySize;

    // Position hotbar at bottom center of screen
    float hotbarWidth = HOTBAR_SLOTS * (HOTBAR_SLOT_SIZE + HOTBAR_PADDING) + HOTBAR_PADDING;
    float hotbarHeight = HOTBAR_SLOT_SIZE + 2 * HOTBAR_PADDING;
    float hotbarX = (displaySize.x - hotbarWidth) * 0.5f;
    float hotbarY = displaySize.y - hotbarHeight - 10.0f;

    ImGui::SetNextWindowPos(ImVec2(hotbarX, hotbarY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(hotbarWidth, hotbarHeight), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                            ImGuiWindowFlags_NoMove |
                            ImGuiWindowFlags_NoResize |
                            ImGuiWindowFlags_NoScrollbar |
                            ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.0f));
    ImGui::Begin("Hotbar", nullptr, flags);

    auto& registry = BlockRegistry::instance();

    // Draw each hotbar slot
    for (int i = 0; i < HOTBAR_SLOTS; i++) {
        ImVec2 slotPos = ImVec2(HOTBAR_PADDING + i * (HOTBAR_SLOT_SIZE + HOTBAR_PADDING), HOTBAR_PADDING);
        ImGui::SetCursorPos(slotPos);

        // Draw slot background
        ImVec4 bgColor = (i == m_selectedHotbarSlot)
            ? ImVec4(0.8f, 0.8f, 0.8f, 0.9f)  // Selected: bright
            : ImVec4(0.2f, 0.2f, 0.2f, 0.7f); // Unselected: dark

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 windowPos = ImGui::GetWindowPos();
        ImVec2 slotMin = ImVec2(windowPos.x + slotPos.x, windowPos.y + slotPos.y);
        ImVec2 slotMax = ImVec2(slotMin.x + HOTBAR_SLOT_SIZE, slotMin.y + HOTBAR_SLOT_SIZE);

        // Draw background
        drawList->AddRectFilled(slotMin, slotMax, ImGui::ColorConvertFloat4ToU32(bgColor), 2.0f);

        // Draw white outline for selected slot
        if (i == m_selectedHotbarSlot) {
            drawList->AddRect(slotMin, slotMax, IM_COL32(255, 255, 255, 255), 2.0f, 0, 3.0f);
        } else {
            drawList->AddRect(slotMin, slotMax, IM_COL32(80, 80, 80, 255), 2.0f, 0, 1.0f);
        }

        // Draw block icon using isometric renderer
        if (m_hotbar[i] > 0 && m_hotbar[i] < registry.count()) {
            ImVec2 iconCenter = ImVec2(slotMin.x + HOTBAR_SLOT_SIZE * 0.5f, slotMin.y + HOTBAR_SLOT_SIZE * 0.5f);
            BlockIconRenderer::drawBlockIcon(drawList, iconCenter, HOTBAR_SLOT_SIZE * 0.7f, m_hotbar[i]);
        }

        // Draw slot number
        ImGui::SetCursorPos(ImVec2(slotPos.x + 2, slotPos.y + 2));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 0.8f));
        ImGui::Text("%d", i);
        ImGui::PopStyleColor();
    }

    ImGui::End();
    ImGui::PopStyleColor();
}

void Inventory::renderInventoryGrid(VulkanRenderer* renderer) {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 displaySize = io.DisplaySize;

    // Inventory window size and position (centered)
    float invWidth = 600.0f;
    float invHeight = 500.0f;
    float invX = (displaySize.x - invWidth) * 0.5f;
    float invY = (displaySize.y - invHeight) * 0.5f;

    ImGui::SetNextWindowPos(ImVec2(invX, invY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(invWidth, invHeight), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                            ImGuiWindowFlags_NoMove |
                            ImGuiWindowFlags_NoResize;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.15f, 0.15f, 0.15f, 0.95f));
    ImGui::Begin("Creative Inventory", nullptr, flags);

    // Title
    ImGui::SetCursorPosX((invWidth - ImGui::CalcTextSize("Creative Inventory").x) * 0.5f);
    ImGui::Text("Creative Inventory");
    ImGui::Separator();

    // Search bar
    renderSearchBar();
    ImGui::Spacing();

    // Block grid with scrolling
    ImGui::BeginChild("BlockGrid", ImVec2(0, -35), true);

    auto& registry = BlockRegistry::instance();
    int column = 0;

    for (int blockID : m_availableBlocks) {
        if (blockID == 0) continue; // Skip air block

        // Filter by search
        if (!isBlockVisible(blockID)) continue;

        if (column > 0) ImGui::SameLine();

        ImGui::PushID(blockID);

        // Draw block slot
        ImVec2 cursorPos = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        const BlockDefinition& block = registry.get(blockID);

        // Background
        ImVec2 slotMin = cursorPos;
        ImVec2 slotMax = ImVec2(cursorPos.x + INVENTORY_SLOT_SIZE, cursorPos.y + INVENTORY_SLOT_SIZE);
        drawList->AddRectFilled(slotMin, slotMax, IM_COL32(50, 50, 50, 255), 2.0f);
        drawList->AddRect(slotMin, slotMax, IM_COL32(100, 100, 100, 255), 2.0f);

        // Block icon using isometric renderer
        ImVec2 iconCenter = ImVec2(cursorPos.x + INVENTORY_SLOT_SIZE * 0.5f, cursorPos.y + INVENTORY_SLOT_SIZE * 0.5f);
        BlockIconRenderer::drawBlockIcon(drawList, iconCenter, INVENTORY_SLOT_SIZE * 0.7f, blockID);

        // Invisible button for clicking
        ImGui::InvisibleButton("slot", ImVec2(INVENTORY_SLOT_SIZE, INVENTORY_SLOT_SIZE));

        if (ImGui::IsItemClicked(0)) {
            handleInventoryClick(blockID);
        }

        // Tooltip with block name
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("%s", block.name.c_str());
            ImGui::EndTooltip();
        }

        ImGui::PopID();

        column++;
        if (column >= INVENTORY_COLUMNS) {
            column = 0;
        }
    }

    ImGui::EndChild();

    // Instructions at bottom
    ImGui::Separator();
    ImGui::TextDisabled("Click a block to add to hotbar | I/ESC to close");

    ImGui::End();
    ImGui::PopStyleColor();

    // Handle clicking outside to close
    if (ImGui::IsMouseClicked(0)) {
        ImVec2 mousePos = io.MousePos;
        if (mousePos.x < invX || mousePos.x > invX + invWidth ||
            mousePos.y < invY || mousePos.y > invY + invHeight) {
            setOpen(false);
        }
    }
}

void Inventory::renderSearchBar() {
    ImGui::Text("Search:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##search", m_searchBuffer, sizeof(m_searchBuffer));
}

bool Inventory::isBlockVisible(int blockID) const {
    if (m_searchBuffer[0] == '\0') return true;

    auto& registry = BlockRegistry::instance();
    if (blockID >= registry.count()) return false;

    const BlockDefinition& block = registry.get(blockID);
    std::string searchLower = m_searchBuffer;
    std::string nameLower = block.name;

    // Convert to lowercase for case-insensitive search
    std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);
    std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);

    return nameLower.find(searchLower) != std::string::npos;
}

void Inventory::handleHotbarInput(GLFWwindow* window) {
    static bool numberKeysPressed[10] = {false};

    // Handle number keys 0-9 for slot selection
    for (int i = 0; i < 10; i++) {
        int key = (i == 0) ? GLFW_KEY_0 : GLFW_KEY_1 + (i - 1);
        bool isPressed = glfwGetKey(window, key) == GLFW_PRESS;

        if (isPressed && !numberKeysPressed[i]) {
            selectSlot(i);
        }
        numberKeysPressed[i] = isPressed;
    }
}

void Inventory::handleInventoryClick(int blockID) {
    // Add clicked block to currently selected hotbar slot
    if (m_selectedHotbarSlot >= 0 && m_selectedHotbarSlot < HOTBAR_SLOTS) {
        m_hotbar[m_selectedHotbarSlot] = blockID;
    }
}

void Inventory::selectSlot(int slot) {
    if (slot >= 0 && slot < HOTBAR_SLOTS) {
        m_selectedHotbarSlot = slot;
    }
}

void Inventory::scrollHotbar(int direction) {
    m_selectedHotbarSlot += direction;

    // Wrap around
    if (m_selectedHotbarSlot < 0) {
        m_selectedHotbarSlot = HOTBAR_SLOTS - 1;
    } else if (m_selectedHotbarSlot >= HOTBAR_SLOTS) {
        m_selectedHotbarSlot = 0;
    }
}

int Inventory::getSelectedBlockID() const {
    if (m_selectedHotbarSlot >= 0 && m_selectedHotbarSlot < HOTBAR_SLOTS) {
        return m_hotbar[m_selectedHotbarSlot];
    }
    return 0; // Air
}

void Inventory::setHotbarSlot(int slot, int blockID) {
    if (slot >= 0 && slot < HOTBAR_SLOTS) {
        m_hotbar[slot] = blockID;
    }
}

void Inventory::handleMouseScroll(double yoffset) {
    if (!m_isOpen) {
        // Scroll through hotbar
        scrollHotbar(yoffset > 0 ? -1 : 1);
    }
}

void Inventory::renderSelectedBlockPreview(VulkanRenderer* renderer) {
    int selectedBlockID = getSelectedBlockID();
    if (selectedBlockID <= 0) return;

    auto& registry = BlockRegistry::instance();
    if (selectedBlockID >= registry.count()) return;

    const BlockDefinition& block = registry.get(selectedBlockID);

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 displaySize = io.DisplaySize;

    // Position in bottom-right corner (with padding)
    float previewSize = 120.0f;
    float padding = 20.0f;
    float boxSize = previewSize + 20.0f;
    float posX = displaySize.x - boxSize - padding;
    float posY = displaySize.y - boxSize - padding - 80.0f; // Above hotbar

    ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(boxSize, boxSize), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                            ImGuiWindowFlags_NoMove |
                            ImGuiWindowFlags_NoResize |
                            ImGuiWindowFlags_NoScrollbar |
                            ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.0f));
    ImGui::Begin("BlockPreview", nullptr, flags);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 windowPos = ImGui::GetWindowPos();

    // Draw background box
    ImVec2 boxMin = windowPos;
    ImVec2 boxMax = ImVec2(windowPos.x + boxSize, windowPos.y + boxSize);
    drawList->AddRectFilled(boxMin, boxMax, IM_COL32(30, 30, 30, 200), 4.0f);
    drawList->AddRect(boxMin, boxMax, IM_COL32(100, 100, 100, 255), 4.0f, 0, 2.0f);

    // Draw large isometric block icon
    ImVec2 iconCenter = ImVec2(windowPos.x + boxSize * 0.5f, windowPos.y + boxSize * 0.5f);
    BlockIconRenderer::drawBlockPreview(drawList, iconCenter, previewSize, selectedBlockID);

    // Draw block name below
    const char* blockName = block.name.c_str();
    ImVec2 textSize = ImGui::CalcTextSize(blockName);
    ImVec2 textPos = ImVec2(windowPos.x + (boxSize - textSize.x) * 0.5f, windowPos.y + boxSize - textSize.y - 5.0f);
    ImGui::SetCursorScreenPos(textPos);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 0.9f));
    ImGui::Text("%s", blockName);
    ImGui::PopStyleColor();

    ImGui::End();
    ImGui::PopStyleColor();
}
