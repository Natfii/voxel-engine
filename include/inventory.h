#pragma once

#include <string>
#include <vector>
#include <imgui.h>
#include <GLFW/glfw3.h>

class BlockRegistry;
class VulkanRenderer;

// Inventory item type
enum class InventoryItemType {
    BLOCK,
    STRUCTURE
};

// Inventory item (can be block or structure)
struct InventoryItem {
    InventoryItemType type;
    int blockID;                // Used when type == BLOCK
    std::string structureName;  // Used when type == STRUCTURE
    std::string displayName;

    InventoryItem() : type(InventoryItemType::BLOCK), blockID(0) {}
    InventoryItem(int id, const std::string& name)
        : type(InventoryItemType::BLOCK), blockID(id), displayName(name) {}
    InventoryItem(const std::string& structName, const std::string& dispName)
        : type(InventoryItemType::STRUCTURE), blockID(-1), structureName(structName), displayName(dispName) {}
};

// Inventory tabs
enum class InventoryTab {
    BLOCKS,
    STRUCTURES
};

// Creative mode inventory system with hotbar and full inventory grid
class Inventory {
public:
    Inventory();
    ~Inventory() = default;

    // Main update and render
    void update(GLFWwindow* window, float deltaTime);
    void render(VulkanRenderer* renderer);
    void renderHotbar(VulkanRenderer* renderer);
    void renderSelectedBlockPreview(VulkanRenderer* renderer);

    // Inventory state
    bool isOpen() const { return m_isOpen; }
    void setOpen(bool open) { m_isOpen = open; }
    void toggleOpen() { m_isOpen = !m_isOpen; }

    // Hotbar management
    int getSelectedSlot() const { return m_selectedHotbarSlot; }
    InventoryItem getSelectedItem() const;
    int getSelectedBlockID() const;  // Legacy compatibility
    void setHotbarSlot(int slot, const InventoryItem& item);
    void selectSlot(int slot);
    void scrollHotbar(int direction);

    // Mouse input for inventory UI
    void handleMouseScroll(double yoffset);

    // Persistence
    bool save(const std::string& worldPath) const;
    bool load(const std::string& worldPath);

private:
    // Inventory state
    bool m_isOpen;
    int m_selectedHotbarSlot; // 0-9 for 10 slots
    std::vector<InventoryItem> m_hotbar; // Items in hotbar slots (10 slots)

    // Tab state
    InventoryTab m_currentTab;

    // Full inventory grid
    std::vector<int> m_availableBlocks; // All block IDs from registry
    std::vector<std::string> m_availableStructures; // All structure names
    float m_inventoryScrollOffset;
    char m_searchBuffer[256];

    // UI rendering helpers
    void renderInventoryGrid(VulkanRenderer* renderer);
    void renderTabs();
    void renderBlocksGrid(VulkanRenderer* renderer);
    void renderStructuresGrid(VulkanRenderer* renderer);
    void renderSearchBar();
    void filterBlocksBySearch();
    bool isBlockVisible(int blockID) const;
    bool isStructureVisible(const std::string& structureName) const;

    // Input handling
    void handleHotbarInput(GLFWwindow* window);
    void handleInventoryClick(const InventoryItem& item);

    // UI layout constants
    static constexpr int HOTBAR_SLOTS = 10;
    static constexpr float HOTBAR_SLOT_SIZE = 50.0f;
    static constexpr float HOTBAR_PADDING = 4.0f;
    static constexpr float INVENTORY_SLOT_SIZE = 48.0f;
    static constexpr int INVENTORY_COLUMNS = 9;
};
