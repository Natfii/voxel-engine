#pragma once

#include <string>
#include <vector>
#include <imgui.h>
#include <GLFW/glfw3.h>

class BlockRegistry;
class VulkanRenderer;

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
    int getSelectedBlockID() const;
    void setHotbarSlot(int slot, int blockID);
    void selectSlot(int slot);
    void scrollHotbar(int direction);

    // Mouse input for inventory UI
    void handleMouseScroll(double yoffset);

private:
    // Inventory state
    bool m_isOpen;
    int m_selectedHotbarSlot; // 0-9 for 10 slots
    std::vector<int> m_hotbar; // Block IDs in hotbar slots (10 slots)

    // Full inventory grid
    std::vector<int> m_availableBlocks; // All block IDs from registry
    float m_inventoryScrollOffset;
    char m_searchBuffer[256];

    // UI rendering helpers
    void renderInventoryGrid(VulkanRenderer* renderer);
    void renderSearchBar();
    void filterBlocksBySearch();
    bool isBlockVisible(int blockID) const;

    // Input handling
    void handleHotbarInput(GLFWwindow* window);
    void handleInventoryClick(int blockID);

    // UI layout constants
    static constexpr int HOTBAR_SLOTS = 10;
    static constexpr float HOTBAR_SLOT_SIZE = 50.0f;
    static constexpr float HOTBAR_PADDING = 4.0f;
    static constexpr float INVENTORY_SLOT_SIZE = 48.0f;
    static constexpr int INVENTORY_COLUMNS = 9;
};
