/**
 * @file LOAD_WORLD_EXAMPLE.cpp
 * @brief Example code showing how to integrate world loading into main.cpp
 *
 * This file demonstrates how to modify the main game loop to support
 * loading existing saved worlds instead of always generating new ones.
 */

// ========================================
// EXAMPLE 1: Simple Auto-Load
// ========================================
// Add this code in main.cpp after world creation and before generateWorld()

// Try to load existing world first
std::string worldPath = "worlds/world_" + std::to_string(seed);
bool worldLoaded = false;

std::cout << "Checking for existing world at: " << worldPath << std::endl;
if (world.loadWorld(worldPath)) {
    std::cout << "Existing world found: " << world.getWorldName() << std::endl;
    worldLoaded = true;

    // Load player state
    if (player.loadPlayerState(worldPath)) {
        std::cout << "Player state loaded" << std::endl;
    } else {
        std::cout << "No saved player state - using default spawn" << std::endl;
    }

    // Load inventory
    if (inventory.load(worldPath)) {
        std::cout << "Inventory loaded" << std::endl;
    } else {
        std::cout << "No saved inventory - using default" << std::endl;
    }

    // Generate meshes for loaded chunks
    std::cout << "Generating meshes for loaded chunks..." << std::endl;
    for (auto* chunk : world.getChunks()) {
        chunk->generateMesh(&world);
    }
} else {
    std::cout << "No existing world found - generating new world" << std::endl;
    // Original world generation code goes here
    world.generateWorld();
    world.decorateWorld();
}

// Continue with buffer creation...
world.createBuffers(&renderer);


// ========================================
// EXAMPLE 2: Menu-Based World Selection
// ========================================
// This shows how to add a world selection menu

#include <filesystem>
#include <vector>

struct WorldInfo {
    std::string name;
    std::string path;
    int seed;
    int width, height, depth;
};

// Function to scan for saved worlds
std::vector<WorldInfo> scanSavedWorlds() {
    namespace fs = std::filesystem;
    std::vector<WorldInfo> worlds;

    if (!fs::exists("worlds")) {
        return worlds;
    }

    for (const auto& entry : fs::directory_iterator("worlds")) {
        if (entry.is_directory()) {
            fs::path metaFile = entry.path() / "world.meta";
            if (fs::exists(metaFile)) {
                WorldInfo info;
                info.path = entry.path().string();

                // Read world metadata
                std::ifstream file(metaFile, std::ios::binary);
                if (file.is_open()) {
                    uint32_t version;
                    file.read(reinterpret_cast<char*>(&version), sizeof(uint32_t));

                    if (version == 1) {
                        file.read(reinterpret_cast<char*>(&info.width), sizeof(int));
                        file.read(reinterpret_cast<char*>(&info.height), sizeof(int));
                        file.read(reinterpret_cast<char*>(&info.depth), sizeof(int));
                        file.read(reinterpret_cast<char*>(&info.seed), sizeof(int));

                        uint32_t nameLength;
                        file.read(reinterpret_cast<char*>(&nameLength), sizeof(uint32_t));
                        info.name.resize(nameLength);
                        file.read(&info.name[0], nameLength);

                        worlds.push_back(info);
                    }
                    file.close();
                }
            }
        }
    }

    return worlds;
}

// Function to display world selection menu with ImGui
int showWorldSelectionMenu(const std::vector<WorldInfo>& worlds) {
    ImGui::Begin("Select World", nullptr, ImGuiWindowFlags_NoCollapse);

    ImGui::Text("Saved Worlds:");
    ImGui::Separator();

    int selectedWorld = -1;

    for (size_t i = 0; i < worlds.size(); i++) {
        const auto& world = worlds[i];

        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Button(world.name.c_str(), ImVec2(300, 40))) {
            selectedWorld = static_cast<int>(i);
        }

        ImGui::SameLine();
        ImGui::Text("Seed: %d | Size: %dx%dx%d",
                   world.seed, world.width, world.height, world.depth);
        ImGui::PopID();
    }

    ImGui::Separator();
    if (ImGui::Button("Create New World", ImVec2(300, 40))) {
        selectedWorld = -2; // Special value for new world
    }

    ImGui::End();

    return selectedWorld;
}

// Usage in main.cpp:
void main() {
    // ... initialization code ...

    // Scan for saved worlds
    auto savedWorlds = scanSavedWorlds();

    bool worldSelected = false;
    int selectedIndex = -1;

    // Show selection menu in a loop
    while (!worldSelected && !glfwWindowShouldClose(window)) {
        glfwPollEvents();
        renderer.beginFrame();

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        selectedIndex = showWorldSelectionMenu(savedWorlds);
        if (selectedIndex >= -1) {
            worldSelected = true;
        }

        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), renderer.getCurrentCommandBuffer());
        renderer.endFrame();
    }

    // Create world based on selection
    World* world = nullptr;
    Player* player = nullptr;
    Inventory* inventory = nullptr;

    if (selectedIndex >= 0) {
        // Load existing world
        const auto& worldInfo = savedWorlds[selectedIndex];
        world = new World(worldInfo.width, worldInfo.height, worldInfo.depth, worldInfo.seed);

        if (world->loadWorld(worldInfo.path)) {
            std::cout << "Loaded world: " << worldInfo.name << std::endl;

            // Create player and inventory
            player = new Player(glm::vec3(0, 50, 0), glm::vec3(0, 1, 0), -90.0f, 0.0f);
            inventory = new Inventory();

            // Load their states
            player->loadPlayerState(worldInfo.path);
            inventory->load(worldInfo.path);

            // Generate meshes
            for (auto* chunk : world->getChunks()) {
                chunk->generateMesh(world);
            }
        }
    } else {
        // Create new world
        int seed = static_cast<int>(std::time(nullptr));
        world = new World(12, 4, 12, seed);
        player = new Player(glm::vec3(0, 50, 0), glm::vec3(0, 1, 0), -90.0f, 0.0f);
        inventory = new Inventory();

        world->generateWorld();
        world->decorateWorld();
    }

    // Continue with game loop...
    world->createBuffers(&renderer);
    // ... rest of game code ...
}


// ========================================
// EXAMPLE 3: Periodic Auto-Save
// ========================================
// Add this to the main game loop to auto-save every 5 minutes

// Add these variables before the game loop
auto lastSaveTime = std::chrono::steady_clock::now();
const auto saveInterval = std::chrono::minutes(5);
std::string worldPath = "worlds/world_" + std::to_string(seed);

// Add this inside the game loop (after update logic, before rendering)
auto currentTime = std::chrono::steady_clock::now();
if (currentTime - lastSaveTime >= saveInterval) {
    std::cout << "Auto-saving..." << std::endl;

    // Save in background (optional: could use async)
    bool saveSuccess = true;
    saveSuccess &= world.saveWorld(worldPath);
    saveSuccess &= player.savePlayerState(worldPath);
    saveSuccess &= inventory.save(worldPath);

    if (saveSuccess) {
        std::cout << "Auto-save complete" << std::endl;
        // Optional: Show brief notification to player
    } else {
        std::cout << "Auto-save failed!" << std::endl;
    }

    lastSaveTime = currentTime;
}


// ========================================
// EXAMPLE 4: Manual Save Command
// ========================================
// Add this to console_commands.cpp to allow manual save via console

void registerSaveCommands() {
    ConsoleCommandRegistry::instance().registerCommand(
        "save",
        "Manually save the world",
        [](const std::vector<std::string>& args, World* world, Player* player, Inventory* inventory) {
            std::string worldPath = "worlds/world_" + std::to_string(world->getSeed());

            bool success = true;
            success &= world->saveWorld(worldPath);
            success &= player->savePlayerState(worldPath);
            success &= inventory->save(worldPath);

            if (success) {
                return "World saved successfully to: " + worldPath;
            } else {
                return "Failed to save world!";
            }
        }
    );

    ConsoleCommandRegistry::instance().registerCommand(
        "save_as",
        "Save world with a custom name: save_as <name>",
        [](const std::vector<std::string>& args, World* world, Player* player, Inventory* inventory) {
            if (args.size() < 2) {
                return "Usage: save_as <world_name>";
            }

            std::string worldPath = "worlds/" + args[1];

            bool success = true;
            success &= world->saveWorld(worldPath);
            success &= player->savePlayerState(worldPath);
            success &= inventory->save(worldPath);

            if (success) {
                return "World saved as: " + args[1];
            } else {
                return "Failed to save world!";
            }
        }
    );
}


// ========================================
// EXAMPLE 5: Save Before Dangerous Operations
// ========================================
// Automatically save before potentially destructive operations

void executeConsoleCommand(const std::string& command, World* world, Player* player, Inventory* inventory) {
    // Check if command is potentially destructive
    bool isDangerous = (command.find("fill") != std::string::npos ||
                       command.find("clear") != std::string::npos ||
                       command.find("generate") != std::string::npos);

    if (isDangerous) {
        // Create backup save
        std::string backupPath = "worlds/backup_" +
                                std::to_string(std::time(nullptr));

        std::cout << "Creating backup before dangerous operation..." << std::endl;
        world->saveWorld(backupPath);
        player->savePlayerState(backupPath);
        inventory->save(backupPath);
        std::cout << "Backup created at: " << backupPath << std::endl;
    }

    // Execute the command
    // ... command execution code ...
}
