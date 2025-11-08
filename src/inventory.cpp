#include "inventory.h"
#include "block_system.h"
#include "structure_system.h"
#include "vulkan_renderer.h"
#include "input_manager.h"
#include <algorithm>
#include <imgui.h>

Inventory::Inventory()
    : m_isOpen(false)
    , m_selectedHotbarSlot(0)
    , m_currentTab(InventoryTab::BLOCKS)
    , m_inventoryScrollOffset(0.0f)
{
    // Initialize hotbar with first 10 blocks (air + first solid blocks)
    m_hotbar.resize(HOTBAR_SLOTS);
    auto& blockRegistry = BlockRegistry::instance();
    for (int i = 0; i < HOTBAR_SLOTS && i < blockRegistry.count(); i++) {
        m_hotbar[i] = InventoryItem(i, blockRegistry.get(i).name);
    }

    // Initialize available blocks from registry
    for (int i = 0; i < blockRegistry.count(); i++) {
        m_availableBlocks.push_back(i);
    }

    // Initialize available structures from registry
    auto& structRegistry = StructureRegistry::instance();
    m_availableStructures = structRegistry.getAllStructureNames();

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

    // Draw each hotbar slot in keyboard order: 1,2,3,4,5,6,7,8,9,0
    for (int visualPos = 0; visualPos < HOTBAR_SLOTS; visualPos++) {
        // Map visual position to actual slot index (slot 0 appears at end)
        int slotIndex = (visualPos + 1) % HOTBAR_SLOTS;  // 1,2,3,4,5,6,7,8,9,0

        ImVec2 slotPos = ImVec2(HOTBAR_PADDING + visualPos * (HOTBAR_SLOT_SIZE + HOTBAR_PADDING), HOTBAR_PADDING);
        ImGui::SetCursorPos(slotPos);

        // Draw slot background
        ImVec4 bgColor = (slotIndex == m_selectedHotbarSlot)
            ? ImVec4(0.8f, 0.8f, 0.8f, 0.9f)  // Selected: bright
            : ImVec4(0.2f, 0.2f, 0.2f, 0.7f); // Unselected: dark

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 windowPos = ImGui::GetWindowPos();
        ImVec2 slotMin = ImVec2(windowPos.x + slotPos.x, windowPos.y + slotPos.y);
        ImVec2 slotMax = ImVec2(slotMin.x + HOTBAR_SLOT_SIZE, slotMin.y + HOTBAR_SLOT_SIZE);

        // Draw background
        drawList->AddRectFilled(slotMin, slotMax, ImGui::ColorConvertFloat4ToU32(bgColor), 2.0f);

        // Draw white outline for selected slot
        if (slotIndex == m_selectedHotbarSlot) {
            drawList->AddRect(slotMin, slotMax, IM_COL32(255, 255, 255, 255), 2.0f, 0, 3.0f);
        } else {
            drawList->AddRect(slotMin, slotMax, IM_COL32(80, 80, 80, 255), 2.0f, 0, 1.0f);
        }

        // Draw item icon using isometric renderer
        const auto& item = m_hotbar[slotIndex];
        ImVec2 iconCenter = ImVec2(slotMin.x + HOTBAR_SLOT_SIZE * 0.5f, slotMin.y + HOTBAR_SLOT_SIZE * 0.5f);

        if (item.type == InventoryItemType::BLOCK) {
            if (item.blockID > 0 && item.blockID < registry.count()) {
                BlockIconRenderer::drawBlockIcon(drawList, iconCenter, HOTBAR_SLOT_SIZE * 0.7f, item.blockID);
            }
        } else if (item.type == InventoryItemType::STRUCTURE) {
            StructureIconRenderer::drawStructureIcon(drawList, iconCenter, HOTBAR_SLOT_SIZE * 0.7f, item.structureName);
        }

        // Draw slot number (keyboard key: 1-9, then 0)
        ImGui::SetCursorPos(ImVec2(slotPos.x + 2, slotPos.y + 2));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 0.8f));
        ImGui::Text("%d", slotIndex);
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

    // Tabs
    renderTabs();
    ImGui::Spacing();

    // Search bar
    renderSearchBar();
    ImGui::Spacing();

    // Render appropriate grid based on active tab
    if (m_currentTab == InventoryTab::BLOCKS) {
        renderBlocksGrid(renderer);
    } else {
        renderStructuresGrid(renderer);
    }

    // Instructions at bottom
    ImGui::Separator();
    ImGui::TextDisabled("Click an item to add to hotbar | I/ESC to close");

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

void Inventory::renderTabs() {
    ImGui::PushStyleColor(ImGuiCol_Button, m_currentTab == InventoryTab::BLOCKS ? ImVec4(0.4f, 0.4f, 0.8f, 1.0f) : ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
    if (ImGui::Button("Blocks", ImVec2(100, 30))) {
        m_currentTab = InventoryTab::BLOCKS;
        m_searchBuffer[0] = '\0';  // Clear search when switching tabs
    }
    ImGui::PopStyleColor();

    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, m_currentTab == InventoryTab::STRUCTURES ? ImVec4(0.4f, 0.4f, 0.8f, 1.0f) : ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
    if (ImGui::Button("Structures", ImVec2(100, 30))) {
        m_currentTab = InventoryTab::STRUCTURES;
        m_searchBuffer[0] = '\0';  // Clear search when switching tabs
    }
    ImGui::PopStyleColor();
}

void Inventory::renderBlocksGrid(VulkanRenderer* renderer) {
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
            handleInventoryClick(InventoryItem(blockID, block.name));
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
}

void Inventory::renderStructuresGrid(VulkanRenderer* renderer) {
    ImGui::BeginChild("StructureGrid", ImVec2(0, -35), true);

    int column = 0;

    for (const auto& structureName : m_availableStructures) {
        // Filter by search
        if (!isStructureVisible(structureName)) continue;

        if (column > 0) ImGui::SameLine();

        ImGui::PushID(structureName.c_str());

        // Draw structure slot
        ImVec2 cursorPos = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // Background
        ImVec2 slotMin = cursorPos;
        ImVec2 slotMax = ImVec2(cursorPos.x + INVENTORY_SLOT_SIZE, cursorPos.y + INVENTORY_SLOT_SIZE);
        drawList->AddRectFilled(slotMin, slotMax, IM_COL32(50, 50, 50, 255), 2.0f);
        drawList->AddRect(slotMin, slotMax, IM_COL32(100, 100, 100, 255), 2.0f);

        // Structure icon using isometric renderer
        ImVec2 iconCenter = ImVec2(cursorPos.x + INVENTORY_SLOT_SIZE * 0.5f, cursorPos.y + INVENTORY_SLOT_SIZE * 0.5f);
        StructureIconRenderer::drawStructureIcon(drawList, iconCenter, INVENTORY_SLOT_SIZE * 0.7f, structureName);

        // Invisible button for clicking
        ImGui::InvisibleButton("slot", ImVec2(INVENTORY_SLOT_SIZE, INVENTORY_SLOT_SIZE));

        if (ImGui::IsItemClicked(0)) {
            handleInventoryClick(InventoryItem(structureName, structureName));
        }

        // Tooltip with structure name
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("%s", structureName.c_str());
            ImGui::EndTooltip();
        }

        ImGui::PopID();

        column++;
        if (column >= INVENTORY_COLUMNS) {
            column = 0;
        }
    }

    ImGui::EndChild();
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

bool Inventory::isStructureVisible(const std::string& structureName) const {
    if (m_searchBuffer[0] == '\0') return true;

    std::string searchLower = m_searchBuffer;
    std::string nameLower = structureName;

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

void Inventory::handleInventoryClick(const InventoryItem& item) {
    // Add clicked item to currently selected hotbar slot
    if (m_selectedHotbarSlot >= 0 && m_selectedHotbarSlot < HOTBAR_SLOTS) {
        m_hotbar[m_selectedHotbarSlot] = item;
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

InventoryItem Inventory::getSelectedItem() const {
    if (m_selectedHotbarSlot >= 0 && m_selectedHotbarSlot < HOTBAR_SLOTS) {
        return m_hotbar[m_selectedHotbarSlot];
    }
    return InventoryItem(); // Default (Air)
}

int Inventory::getSelectedBlockID() const {
    if (m_selectedHotbarSlot >= 0 && m_selectedHotbarSlot < HOTBAR_SLOTS) {
        const auto& item = m_hotbar[m_selectedHotbarSlot];
        if (item.type == InventoryItemType::BLOCK) {
            return item.blockID;
        }
    }
    return 0; // Air
}

void Inventory::setHotbarSlot(int slot, const InventoryItem& item) {
    if (slot >= 0 && slot < HOTBAR_SLOTS) {
        m_hotbar[slot] = item;
    }
}

void Inventory::handleMouseScroll(double yoffset) {
    if (!m_isOpen) {
        // Scroll through hotbar
        scrollHotbar(yoffset > 0 ? -1 : 1);
    }
}

void Inventory::renderSelectedBlockPreview(VulkanRenderer* renderer) {
    InventoryItem selectedItem = getSelectedItem();
    if (selectedItem.type == InventoryItemType::BLOCK && selectedItem.blockID <= 0) return;

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
    ImGui::Begin("ItemPreview", nullptr, flags);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 windowPos = ImGui::GetWindowPos();

    // Draw background box
    ImVec2 boxMin = windowPos;
    ImVec2 boxMax = ImVec2(windowPos.x + boxSize, windowPos.y + boxSize);
    drawList->AddRectFilled(boxMin, boxMax, IM_COL32(30, 30, 30, 200), 4.0f);
    drawList->AddRect(boxMin, boxMax, IM_COL32(100, 100, 100, 255), 4.0f, 0, 2.0f);

    // Draw large isometric icon
    ImVec2 iconCenter = ImVec2(windowPos.x + boxSize * 0.5f, windowPos.y + boxSize * 0.5f);

    if (selectedItem.type == InventoryItemType::BLOCK) {
        auto& registry = BlockRegistry::instance();
        if (selectedItem.blockID < registry.count()) {
            BlockIconRenderer::drawBlockPreview(drawList, iconCenter, previewSize, selectedItem.blockID);
        }
    } else if (selectedItem.type == InventoryItemType::STRUCTURE) {
        StructureIconRenderer::drawStructurePreview(drawList, iconCenter, previewSize, selectedItem.structureName);
    }

    // Draw item name below
    const char* itemName = selectedItem.displayName.c_str();
    ImVec2 textSize = ImGui::CalcTextSize(itemName);
    ImVec2 textPos = ImVec2(windowPos.x + (boxSize - textSize.x) * 0.5f, windowPos.y + boxSize - textSize.y - 5.0f);
    ImGui::SetCursorScreenPos(textPos);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 0.9f));
    ImGui::Text("%s", itemName);
    ImGui::PopStyleColor();

    ImGui::End();
    ImGui::PopStyleColor();
}
