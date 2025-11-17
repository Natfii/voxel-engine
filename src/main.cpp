/**
 * @file main.cpp
 * @brief Main entry point for the voxel engine application
 *
 * This file initializes and runs the main game loop, coordinating:
 * - GLFW window and input management
 * - Vulkan renderer initialization
 * - World generation and chunk management
 * - Player controller and physics
 * - ImGui-based developer console and debug UI
 * - Pause menu and input handling
 *
 * Created by original author
 */

#include <iostream>
#include <cmath>
#include <stdexcept>
// GLFW header
#include <GLFW/glfw3.h>
// GLM for matrix transformations
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
// ImGui headers
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
// Project headers
#include "vulkan_renderer.h"
#include "chunk.h"
#include "world.h"
#include "block_system.h"
#include "biome_system.h"
#include "structure_system.h"
#include "player.h"
#include "pause_menu.h"
#include "main_menu.h"
#include "config.h"
#include "console.h"
#include "console_commands.h"
#include "debug_state.h"
#include "targeting_system.h"
#include "raycast.h"
#include "input_manager.h"
#include "inventory.h"
#include "terrain_constants.h"
// BlockIconRenderer is now part of block_system.h

// Game state
enum class GameState {
    MAIN_MENU,
    IN_GAME
};

// Global variables
VulkanRenderer* g_renderer = nullptr;
Inventory* g_inventory = nullptr;

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    if (g_renderer) {
        g_renderer->framebufferResized();
    }
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    if (g_inventory) {
        g_inventory->handleMouseScroll(yoffset);
    }
}

float deltaTime = 0.0f;
float lastFrame = 0.0f;

// ImGui Vulkan init helper
static void check_vk_result(VkResult err) {
    if (err == 0) return;
    std::cerr << "[vulkan] Error: VkResult = " << err << std::endl;
    if (err < 0)
        abort();
}

int main() {
    try {
        // Load configuration first
        Config& config = Config::instance();
        if (!config.loadFromFile("config.ini")) {
            std::cerr << "Warning: Failed to load config.ini, using default values" << std::endl;
        }

        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // No OpenGL context
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);     // Allow window resizing

        // Load window size from config
        int windowWidth = config.getInt("Window", "width", 800);
        int windowHeight = config.getInt("Window", "height", 600);

        GLFWwindow* window = glfwCreateWindow(windowWidth, windowHeight, "Voxel Engine - Vulkan", nullptr, nullptr);
        if (!window) {
            std::cerr << "Failed to create GLFW window\n";
            glfwTerminate();
            return -1;
        }
        glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
        glfwSetScrollCallback(window, scroll_callback);
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

        // Initialize Vulkan renderer
        std::cout << "Initializing Vulkan renderer..." << std::endl;
        VulkanRenderer renderer(window);
        g_renderer = &renderer;

        // Setup ImGui context
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();

        // Setup ImGui for GLFW + Vulkan
        ImGui_ImplGlfw_InitForVulkan(window, true);

        // Create separate descriptor pool for ImGui
        VkDescriptorPoolSize pool_sizes[] = {
            { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
        };
        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 1000;
        pool_info.poolSizeCount = std::size(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;
        VkDescriptorPool imguiPool;
        if (vkCreateDescriptorPool(renderer.getDevice(), &pool_info, nullptr, &imguiPool) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create ImGui descriptor pool");
        }

        // Initialize ImGui Vulkan backend
        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance = renderer.getInstance();
        init_info.PhysicalDevice = renderer.getPhysicalDevice();
        init_info.Device = renderer.getDevice();
        init_info.Queue = renderer.getGraphicsQueue();
        init_info.DescriptorPool = imguiPool;
        init_info.MinImageCount = 2;
        init_info.ImageCount = 2;
        init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        init_info.CheckVkResultFn = check_vk_result;
        init_info.RenderPass = renderer.getRenderPass();

        ImGui_ImplVulkan_Init(&init_info);

        // Upload ImGui fonts
        ImGui_ImplVulkan_CreateFontsTexture();
        // Note: ImGui will handle font upload in the next frame

        // ========== LOADING SCREEN SYSTEM ==========
        // Helper to render loading screen with progress and animated dots
        float loadingProgress = 0.0f;
        std::string loadingMessage = "Initializing";
        bool loadingComplete = false;
        int dotAnimationFrame = 0;

        auto renderLoadingScreen = [&]() {
            if (loadingComplete) return;

            if (!renderer.beginFrame()) return;

            // Animate dots pattern: .  ..  ...  .  ..  ... (cycles every 6 frames)
            const char* dotPatterns[] = {".", "..", "...", ".", "..", "..."};
            std::string animatedMessage = loadingMessage + dotPatterns[dotAnimationFrame % 6];
            dotAnimationFrame++;

            // Start ImGui frame
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            // Full-screen black overlay
            ImGuiIO& io = ImGui::GetIO();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::Begin("LoadingOverlay", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs);

            // Center the loading UI
            float centerX = io.DisplaySize.x * 0.5f;
            float centerY = io.DisplaySize.y * 0.5f;

            // Loading text with animated dots
            ImGui::SetCursorPos(ImVec2(centerX - 150, centerY - 50));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            ImGui::Text("%s", animatedMessage.c_str());
            ImGui::PopStyleColor();

            // Progress bar
            ImGui::SetCursorPos(ImVec2(centerX - 150, centerY));
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.3f, 0.7f, 0.3f, 1.0f));
            ImGui::ProgressBar(loadingProgress, ImVec2(300, 30), "");
            ImGui::PopStyleColor();

            // Percentage text
            ImGui::SetCursorPos(ImVec2(centerX - 30, centerY + 40));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
            ImGui::Text("%.0f%%", loadingProgress * 100.0f);
            ImGui::PopStyleColor();

            ImGui::End();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor();

            // Render ImGui
            ImGui::Render();
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), renderer.getCurrentCommandBuffer());

            renderer.endFrame();
            glfwPollEvents();  // Keep window responsive
        };

        // ========== MAIN MENU ==========
        // Show main menu and wait for player to start game
        std::cout << "Showing main menu..." << std::endl;
        MainMenu mainMenu(window);
        GameState gameState = GameState::MAIN_MENU;
        int seed = config.getInt("World", "seed", 1124345);  // Default seed
        bool shouldQuit = false;

        // Enable cursor for menu
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        InputManager::instance().setContext(InputManager::Context::MAIN_MENU);

        while (gameState == GameState::MAIN_MENU && !glfwWindowShouldClose(window)) {
            glfwPollEvents();

            // Begin frame for menu rendering
            if (!renderer.beginFrame()) {
                continue;
            }

            // Start ImGui frame
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            // Render main menu and get result
            MenuResult menuResult = mainMenu.render();

            // Process menu result
            if (menuResult.action == MenuAction::NEW_GAME) {
                seed = menuResult.seed;
                gameState = GameState::IN_GAME;
                std::cout << "Starting new game with seed: " << seed << std::endl;
            } else if (menuResult.action == MenuAction::LOAD_GAME) {
                // TODO: Implement load game
                std::cout << "Load game not yet implemented" << std::endl;
            } else if (menuResult.action == MenuAction::QUIT) {
                shouldQuit = true;
                break;
            }

            // Render ImGui
            ImGui::Render();
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), renderer.getCurrentCommandBuffer());

            renderer.endFrame();
        }

        // If player quit from menu, exit early
        if (shouldQuit || glfwWindowShouldClose(window)) {
            std::cout << "Exiting from main menu..." << std::endl;

            // Cleanup ImGui
            ImGui_ImplVulkan_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
            vkDestroyDescriptorPool(renderer.getDevice(), imguiPool, nullptr);

            glfwDestroyWindow(window);
            glfwTerminate();
            return 0;
        }

        // Disable cursor for gameplay
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

        // Get world configuration from config file (use seed from menu)
        int worldWidth = config.getInt("World", "world_width", 12);
        int worldDepth = config.getInt("World", "world_depth", 12);

        // World height: default 3 chunks (96 blocks) for fast startup
        // Can be increased in config.ini for taller worlds
        // Note: Streaming system can load additional chunks on-demand
        int worldHeight = config.getInt("World", "world_height", 3);

        // Loading stage 1: Block registry (10%)
        loadingProgress = 0.05f;
        loadingMessage = "Loading blocks and textures";
        renderLoadingScreen();
        std::cout << "Loading block registry with textures..." << std::endl;
        BlockRegistry::instance().loadBlocks("assets/blocks", &renderer);

        // Loading stage 2: Structure registry (15%)
        loadingProgress = 0.15f;
        loadingMessage = "Loading structures";
        renderLoadingScreen();
        std::cout << "Loading structure registry..." << std::endl;
        StructureRegistry::instance().loadStructures("assets/structures");

        // Loading stage 3: Biome registry (20%)
        loadingProgress = 0.20f;
        loadingMessage = "Loading biomes";
        renderLoadingScreen();
        std::cout << "Loading biome registry..." << std::endl;
        BiomeRegistry::getInstance().loadBiomes("assets/biomes");

        // Loading stage 4: Bind textures (25%)
        loadingProgress = 0.25f;
        loadingMessage = "Setting up renderer";
        renderLoadingScreen();
        std::cout << "Binding texture atlas..." << std::endl;
        renderer.bindAtlasTexture(
            BlockRegistry::instance().getAtlasImageView(),
            BlockRegistry::instance().getAtlasSampler()
        );

        // Create ImGui descriptor set for the texture atlas (for inventory icons)
        std::cout << "Creating ImGui atlas descriptor..." << std::endl;
        VkDescriptorSet atlasImGuiDescriptor = ImGui_ImplVulkan_AddTexture(
            BlockRegistry::instance().getAtlasSampler(),
            BlockRegistry::instance().getAtlasImageView(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        );
        BlockIconRenderer::init(atlasImGuiDescriptor);

        // Loading stage 5: Initialize world (30%)
        loadingProgress = 0.30f;
        loadingMessage = "Initializing world generator";
        renderLoadingScreen();
        std::cout << "Initializing world generation..." << std::endl;
        Chunk::initNoise(seed);
        World world(worldWidth, worldHeight, worldDepth, seed);

        // Loading stage 6: Generate spawn area only (much faster than full world)
        // With 320 chunk height, generating all 46,080 chunks takes forever
        // Instead, generate just a small area around spawn and let streaming handle the rest
        loadingProgress = 0.35f;
        loadingMessage = "Generating spawn area";
        renderLoadingScreen();
        std::cout << "Generating spawn chunks (streaming will handle the rest)..." << std::endl;

        // We'll determine spawn chunk coordinates and generate them before finding exact spawn point
        // Assume spawn will be somewhere near (0, 64, 0) in world space
        // That's chunk (0, 2, 0) in chunk coordinates if spawn height is 64 blocks
        // For centered world with 320 height: Y chunks from -160 to +159
        // Spawn at Y=64 is chunk Y=2 (chunk 2 * 32 = 64-95 blocks)
        int spawnChunkX = 0;
        int spawnChunkY = 2;  // Y=64 surface is in chunk Y=2
        int spawnChunkZ = 0;
        int spawnRadius = 4;  // Generate 9x9x9 chunks = 729 chunks (reasonable startup time)

        world.generateSpawnChunks(spawnChunkX, spawnChunkY, spawnChunkZ, spawnRadius);

        // Decorate world with trees and features
        // Only decorates spawn chunks for faster startup
        std::cout << "Placing trees and features..." << std::endl;
        world.decorateWorld();

        // Register water blocks with simulation system
        std::cout << "Initializing water physics..." << std::endl;
        world.registerWaterBlocks();

        // Loading stage 8: Create GPU buffers (85%)
        loadingProgress = 0.80f;
        loadingMessage = "Creating GPU buffers";
        renderLoadingScreen();
        std::cout << "Creating GPU buffers..." << std::endl;
        world.createBuffers(&renderer);

        // Loading stage 9: Finding spawn location (90%)
        loadingProgress = 0.88f;
        loadingMessage = "Finding safe spawn location";
        renderLoadingScreen();

        // Improved spawn logic: Search for safe flat area with solid ground (no caves below)
        float spawnX = 0.0f;
        float spawnZ = 0.0f;
        int spawnGroundY = -1;

        std::cout << "Finding safe spawn location..." << std::endl;

        // World is centered around Y=0 with both positive and negative chunks
        const int halfHeight = worldHeight / 2;
        const int MIN_WORLD_Y = -halfHeight * 32;  // Minimum Y coordinate (deep caves)
        const int MAX_WORLD_Y = (worldHeight - halfHeight) * 32 - 1;  // Maximum Y coordinate (sky)

        // Search for spawn on surface (typically around Y=64) down to minimum safe depth
        const int MAX_TERRAIN_HEIGHT = std::min(180, MAX_WORLD_Y);  // Don't search above world bounds
        const int MIN_SEARCH_Y = std::max(10, MIN_WORLD_Y + 20);  // Don't spawn too close to void
        const int SEARCH_RADIUS = 32;  // Search in 32 block radius
        const int MIN_SOLID_DEPTH = 5;  // Require at least 5 blocks of solid ground below

        std::cout << "World Y range: " << MIN_WORLD_Y << " to " << MAX_WORLD_Y
                  << " (" << (MAX_WORLD_Y - MIN_WORLD_Y + 1) << " blocks)" << std::endl;
        std::cout << "Searching for spawn from Y=" << MAX_TERRAIN_HEIGHT << " down to Y=" << MIN_SEARCH_Y << std::endl;

        // Helper to check if a location is safe to spawn
        auto isSafeSpawn = [&world, MIN_SOLID_DEPTH, MAX_TERRAIN_HEIGHT](float x, float z, int groundY) -> bool {
            // Bounds check
            if (groundY < MIN_SOLID_DEPTH || groundY >= MAX_TERRAIN_HEIGHT - 4) {
                return false;  // Too close to terrain boundaries
            }

            // Check if there's solid ground below (no caves)
            for (int dy = 0; dy < MIN_SOLID_DEPTH; dy++) {
                int blockID = world.getBlockAt(x, static_cast<float>(groundY - dy), z);
                // Must be solid (not air=0, not water=5)
                if (blockID == 0 || blockID == 5) {
                    return false;  // Cave or water underneath
                }
            }
            // Check if there's clear space above for player (2 blocks tall)
            // Player feet will be at groundY+1, head at groundY+3 (1.8 blocks tall rounded up to 2)
            for (int dy = 1; dy <= 4; dy++) {
                int blockID = world.getBlockAt(x, static_cast<float>(groundY + dy), z);
                if (blockID != 0) {
                    return false;  // Not enough headroom
                }
            }
            return true;
        };

        // Spiral search pattern outward from (0, 0)
        bool foundSafeSpawn = false;
        for (int radius = 0; radius <= SEARCH_RADIUS && !foundSafeSpawn; radius++) {
            // Search at this radius
            for (int dx = -radius; dx <= radius && !foundSafeSpawn; dx++) {
                for (int dz = -radius; dz <= radius && !foundSafeSpawn; dz++) {
                    // Only check perimeter of this radius (optimization)
                    // Special case: when radius=0, check the center point (0,0)
                    if (radius > 0 && abs(dx) != radius && abs(dz) != radius) continue;

                    float testX = static_cast<float>(dx);
                    float testZ = static_cast<float>(dz);

                    // Start search from MAX_TERRAIN_HEIGHT down to MIN_SEARCH_Y
                    for (int y = MAX_TERRAIN_HEIGHT; y >= MIN_SEARCH_Y; y--) {  // Search from top of terrain down
                        int currentBlock = world.getBlockAt(testX, static_cast<float>(y), testZ);
                        int aboveBlock = world.getBlockAt(testX, static_cast<float>(y + 1), testZ);

                        // Found potential surface: current is solid AND above is air
                        if ((currentBlock != 0 && currentBlock != 5) && aboveBlock == 0) {
                            // Check if this is a safe spawn (solid ground below, clear above)
                            if (isSafeSpawn(testX, testZ, y)) {
                                spawnX = testX;
                                spawnZ = testZ;
                                spawnGroundY = y;
                                foundSafeSpawn = true;
                                std::cout << "Found safe spawn at (" << testX << ", " << y << ", " << testZ
                                          << ") with solid ground below (block ID: " << currentBlock << ")" << std::endl;
                                break;
                            }
                        }
                    }
                }
            }
        }

        // Emergency fallback if no safe spawn found
        if (spawnGroundY < 0) {
            std::cout << "WARNING: No safe spawn found in initial search, validating fallback at (0, 0, 64)" << std::endl;

            // Try to find ground at (0,0) by searching downward from MAX_TERRAIN_HEIGHT
            bool foundFallback = false;
            for (int y = MAX_TERRAIN_HEIGHT; y >= MIN_SEARCH_Y; y--) {
                int currentBlock = world.getBlockAt(0.0f, static_cast<float>(y), 0.0f);
                int aboveBlock = world.getBlockAt(0.0f, static_cast<float>(y + 1), 0.0f);

                if ((currentBlock != 0 && currentBlock != 5) && aboveBlock == 0) {
                    if (isSafeSpawn(0.0f, 0.0f, y)) {
                        spawnX = 0.0f;
                        spawnZ = 0.0f;
                        spawnGroundY = y;
                        foundFallback = true;
                        std::cout << "Fallback spawn validated at (0, " << y << ", 0)" << std::endl;
                        break;
                    }
                }
            }

            // If fallback at (0,0) also failed, do expanding radius search with relaxed constraints
            if (!foundFallback) {
                std::cout << "WARNING: Fallback at (0,0) failed, searching in expanding radius..." << std::endl;
                for (int radius = 1; radius <= 64 && !foundFallback; radius += 4) {
                    for (int dx = -radius; dx <= radius && !foundFallback; dx += 4) {
                        for (int dz = -radius; dz <= radius && !foundFallback; dz += 4) {
                            for (int y = MAX_TERRAIN_HEIGHT; y >= MIN_SEARCH_Y; y--) {
                                float testX = static_cast<float>(dx);
                                float testZ = static_cast<float>(dz);
                                int currentBlock = world.getBlockAt(testX, static_cast<float>(y), testZ);
                                int aboveBlock = world.getBlockAt(testX, static_cast<float>(y + 1), testZ);

                                if ((currentBlock != 0 && currentBlock != 5) && aboveBlock == 0) {
                                    if (isSafeSpawn(testX, testZ, y)) {
                                        spawnX = testX;
                                        spawnZ = testZ;
                                        spawnGroundY = y;
                                        foundFallback = true;
                                        std::cout << "Emergency spawn found at (" << testX << ", " << y << ", " << testZ << ")" << std::endl;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }

                // Last resort: spawn at safe Y calculated from world bounds
                if (!foundFallback) {
                    int emergencyY = (MAX_TERRAIN_HEIGHT + MIN_SEARCH_Y) / 2;  // Midpoint of search range
                    std::cout << "CRITICAL WARNING: No safe spawn found anywhere, using unvalidated Y="
                              << emergencyY << " at (0, 0)" << std::endl;
                    spawnX = 0.0f;
                    spawnZ = 0.0f;
                    spawnGroundY = emergencyY;
                }
            }
        }

        // Calculate spawn Y position
        // Player.Position represents EYE level (camera position), which is 1.6 blocks above feet
        // We want feet slightly above groundY+1 (not exactly on block boundary to avoid collision bugs)
        // So eyes should be at groundY+1.1+1.6 = groundY+2.7
        float spawnY = static_cast<float>(spawnGroundY) + 2.7f;

        // Debug: Check blocks around spawn (extended range to detect caves)
        std::cout << "Blocks at spawn location:" << std::endl;
        for (int dy = -10; dy <= 5; dy++) {
            int blockY = spawnGroundY + dy;
            int blockID = world.getBlockAt(spawnX, static_cast<float>(blockY), spawnZ);
            const char* marker = "";
            if (dy == 0) marker = " <- GROUND";
            else if (dy == 1) marker = " <- FEET";
            else if (dy == 2) marker = " <- HEAD";

            std::cout << "  Y=" << blockY << ": blockID=" << blockID;
            if (blockID == 0) std::cout << " (AIR)";
            else if (blockID == 1) std::cout << " (STONE)";
            else if (blockID == 2) std::cout << " (GRASS)";
            else if (blockID == 3) std::cout << " (DIRT)";
            else if (blockID == 5) std::cout << " (WATER)";
            else if (blockID == 12) std::cout << " (BEDROCK)";
            std::cout << marker << std::endl;
        }

        std::cout << "Spawn at (" << spawnX << ", " << spawnY << ", " << spawnZ
                  << ") - surface Y=" << spawnGroundY << std::endl;

        // CRITICAL DEBUG: Verify blocks exist where we think they do
        std::cout << "\n=== SPAWN VERIFICATION ===" << std::endl;
        float feetY = spawnY - 1.6f;
        std::cout << "Player feet will be at Y=" << feetY << std::endl;
        int groundBlock = world.getBlockAt(spawnX, static_cast<float>(spawnGroundY), spawnZ);
        int feetBlock = world.getBlockAt(spawnX, feetY, spawnZ);
        int belowFeet = world.getBlockAt(spawnX, feetY - 0.1f, spawnZ);
        std::cout << "Block at ground (" << spawnGroundY << "): " << groundBlock << std::endl;
        std::cout << "Block at feet (" << feetY << "): " << feetBlock << std::endl;
        std::cout << "Block 0.1 below feet: " << belowFeet << std::endl;
        if (groundBlock == 0 || feetBlock != 0 || belowFeet == 0) {
            std::cout << "ERROR: Spawn validation FAILED! Terrain doesn't match expectations!" << std::endl;
            std::cout << "  Expected: ground=" << spawnGroundY << " should be solid (got " << groundBlock << ")" << std::endl;
            std::cout << "  Expected: feet position should be air (got " << feetBlock << ")" << std::endl;
            std::cout << "  Expected: below feet should be solid (got " << belowFeet << ")" << std::endl;
        }
        std::cout << "===========================\n" << std::endl;

        // Loading stage 10: Spawning player (95%)
        loadingProgress = 0.95f;
        loadingMessage = "Spawning player";
        renderLoadingScreen();
        Player player(glm::vec3(spawnX, spawnY, spawnZ), glm::vec3(0.0f, 1.0f, 0.0f), -90.0f, 0.0f);

        // Loading stage 11: Initializing game systems (98%)
        loadingProgress = 0.98f;
        loadingMessage = "Initializing game systems";
        renderLoadingScreen();
        PauseMenu pauseMenu(window);

        // Create targeting system (replaces crosshair + block_outline)
        TargetingSystem targetingSystem;
        targetingSystem.init(&renderer);

        // Create console and register commands
        Console console(window);
        ConsoleCommands::registerAll(&console, &player, &world, &renderer);

        // Create inventory system
        Inventory inventory;
        g_inventory = &inventory;

        // Loading stage 12: Final check - wait for player to be on ground (100%)
        loadingProgress = 0.99f;
        loadingMessage = "Ready";
        renderLoadingScreen();

        // Verify player is safely on the ground before hiding loading screen
        // Update physics once to ensure player settles (disable input during this)
        player.update(window, 0.016f, &world, false);  // 1 frame at 60fps, no input

        // Final loading screen frame at 100%
        loadingProgress = 1.0f;
        loadingMessage = "Ready";
        renderLoadingScreen();

        // Hide loading screen - game is ready!
        loadingComplete = true;

        bool isPaused = false;
        bool escPressed = false;
        bool requestMouseReset = false;
        bool f9Pressed = false;
        bool iPressed = false;

        std::cout << "Entering main loop..." << std::endl;

        while (!glfwWindowShouldClose(window)) {
            float currentFrame = static_cast<float>(glfwGetTime());
            deltaTime = currentFrame - lastFrame;
            lastFrame = currentFrame;

            glfwPollEvents();

            // Update FPS counter
            DebugState::instance().updateFPS(deltaTime);

            // Update sky time (handles day/night cycle)
            ConsoleCommands::updateSkyTime(deltaTime);

            // Handle F9 key for console
            if (glfwGetKey(window, GLFW_KEY_F9) == GLFW_PRESS) {
                if (!f9Pressed) {
                    f9Pressed = true;
                    console.toggle();

                    // Hide cursor when console is visible (text input uses keyboard only)
                    if (console.isVisible()) {
                        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
                    } else if (!isPaused && !inventory.isOpen()) {
                        requestMouseReset = true;
                    }
                }
            } else {
                f9Pressed = false;
            }

            // Handle I key for inventory (only when not in console or paused)
            if (glfwGetKey(window, GLFW_KEY_I) == GLFW_PRESS) {
                if (!iPressed && !console.isVisible() && !isPaused) {
                    iPressed = true;
                    inventory.toggleOpen();

                    // Show cursor when inventory is open
                    if (inventory.isOpen()) {
                        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                    } else {
                        requestMouseReset = true;
                    }
                }
            } else {
                iPressed = false;
            }

            // Handle ESC key for pause menu (but not if console or inventory is open)
            if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
                if (!escPressed) {
                    escPressed = true;

                    // If inventory is open, close it first
                    if (inventory.isOpen()) {
                        inventory.setOpen(false);
                        if (!isPaused && !console.isVisible()) {
                            requestMouseReset = true;
                        }
                    }
                    // If console is open, close it instead of opening pause menu
                    else if (console.isVisible()) {
                        console.setVisible(false);
                        if (!isPaused) {
                            requestMouseReset = true;
                        }
                    } else {
                        isPaused = !isPaused;
                        if (isPaused) {
                            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                            // Center cursor on window when pausing
                            int windowWidth, windowHeight;
                            glfwGetWindowSize(window, &windowWidth, &windowHeight);
                            glfwSetCursorPos(window, windowWidth / 2.0, windowHeight / 2.0);
                        } else {
                            requestMouseReset = true;
                        }
                    }
                }
            } else {
                escPressed = false;
            }

            // Update input context based on menu/console/inventory state
            if (isPaused) {
                InputManager::instance().setContext(InputManager::Context::PAUSED);
            } else if (console.isVisible()) {
                InputManager::instance().setContext(InputManager::Context::CONSOLE);
            } else if (inventory.isOpen()) {
                InputManager::instance().setContext(InputManager::Context::INVENTORY);
            } else {
                InputManager::instance().setContext(InputManager::Context::GAMEPLAY);
            }

            // Always update player physics, but only process input during gameplay
            bool canProcessInput = InputManager::instance().canMove();
            player.update(window, deltaTime, &world, canProcessInput);

            // Update inventory system
            inventory.update(window, deltaTime);

            // Particles disabled for performance
            // world.getParticleSystem()->update(deltaTime);

            // DISABLED: Liquid physics causes catastrophic lag (scans 14 million blocks)
            // TODO: Implement dirty list of changed water blocks instead of full world scan
            // Current implementation scans ALL 432 chunks × 32,768 blocks = 14M blocks per update
            /*
            static float liquidUpdateTimer = 0.0f;
            liquidUpdateTimer += deltaTime;
            const float liquidUpdateInterval = 1.0f;
            if (liquidUpdateTimer >= liquidUpdateInterval) {
                liquidUpdateTimer = 0.0f;
                world.updateLiquids(&renderer);
            }
            */

            // Check if console/inventory was closed (by clicking outside or ESC)
            // and we need to reset mouse for gameplay
            static bool wasConsoleOpen = false;
            static bool wasInventoryOpen = false;
            if (wasConsoleOpen && !console.isVisible() && !isPaused && !inventory.isOpen()) {
                requestMouseReset = true;
            }
            if (wasInventoryOpen && !inventory.isOpen() && !isPaused && !console.isVisible()) {
                requestMouseReset = true;
            }
            wasConsoleOpen = console.isVisible();
            wasInventoryOpen = inventory.isOpen();

            if (requestMouseReset) {
                player.resetMouse();
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                requestMouseReset = false;
            }

            // Calculate matrices
            glm::mat4 model = glm::mat4(1.0f);
            glm::mat4 view = player.getViewMatrix();
            int width, height;
            glfwGetFramebufferSize(window, &width, &height);
            float aspect = float(width) / float(height);
            // Near plane at 0.01 allows getting very close to blocks without clipping
            glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, 0.01f, 300.0f);
            // Flip Y axis for Vulkan (Vulkan's Y axis points down in NDC, OpenGL's points up)
            projection[1][1] *= -1;

            // Calculate view-projection matrix for frustum culling
            glm::mat4 viewProj = projection * view;

            // Update uniform buffer with camera position and render distance for fog
            // Increased for wider biomes and better fog visibility
            const float renderDistance = 120.0f;

            // Detect if camera is specifically underwater (not just feet in water)
            bool underwater = player.isCameraUnderwater();
            glm::vec3 liquidFogColor(0.1f, 0.3f, 0.5f);
            float liquidFogStart = 1.0f;
            float liquidFogEnd = 8.0f;
            glm::vec3 liquidTintColor(0.4f, 0.7f, 1.0f);
            float liquidDarkenFactor = 0.4f;

            if (underwater) {
                // Get the block at camera position
                // Convert player position to block coordinates, then back to world coordinates
                int camX = static_cast<int>(std::floor(player.Position.x));
                int camY = static_cast<int>(std::floor(player.Position.y));
                int camZ = static_cast<int>(std::floor(player.Position.z));
                // getBlockAt expects world coordinates (floats), not block coordinates (ints)
                int liquidBlockID = world.getBlockAt(static_cast<float>(camX), static_cast<float>(camY), static_cast<float>(camZ));

                // If it's a liquid block, use its properties from YAML
                auto& registry = BlockRegistry::instance();
                if (liquidBlockID > 0 && liquidBlockID < registry.count()) {
                    const auto& blockDef = registry.get(liquidBlockID);
                    if (blockDef.isLiquid) {
                        liquidFogColor = blockDef.liquidProps.fogColor;
                        liquidFogStart = blockDef.liquidProps.fogStart;
                        liquidFogEnd = blockDef.liquidProps.fogEnd;
                        liquidTintColor = blockDef.liquidProps.tintColor;
                        liquidDarkenFactor = blockDef.liquidProps.darkenFactor;
                    }
                }
            }

            renderer.updateUniformBuffer(renderer.getCurrentFrame(), model, view, projection, player.Position, renderDistance, underwater,
                                       liquidFogColor, liquidFogStart, liquidFogEnd, liquidTintColor, liquidDarkenFactor);

            // Update targeting system (single raycast per frame!)
            targetingSystem.setEnabled(InputManager::instance().isGameplayEnabled());
            targetingSystem.update(&world, player.Position, player.Front);

            // Update block outline buffer if target changed
            const TargetInfo& target = targetingSystem.getTarget();
            if (target.hasTarget) {
                targetingSystem.updateOutlineBuffer(&renderer);
            }

            // Handle block breaking on left mouse click
            static bool leftMousePressed = false;
            if (InputManager::instance().canBreakBlocks() &&
                glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
                if (!leftMousePressed && target.isValid() && target.isBreakable) {
                    leftMousePressed = true;
                    world.breakBlock(target.blockPosition, &renderer);
                }
            } else {
                leftMousePressed = false;
            }

            // Handle block/structure placement on right mouse click (creative mode)
            static bool rightMousePressed = false;
            if (InputManager::instance().canPlaceBlocks() &&
                glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
                if (!rightMousePressed && target.hasTarget) {
                    rightMousePressed = true;

                    // Get selected item from hotbar
                    InventoryItem selectedItem = inventory.getSelectedItem();

                    if (selectedItem.type == InventoryItemType::BLOCK) {
                        // Place block adjacent to the targeted block (using hit normal)
                        // Bounds check to prevent crash from invalid block IDs
                        auto& registry = BlockRegistry::instance();
                        if (selectedItem.blockID > 0 && selectedItem.blockID < registry.count()) {
                            glm::vec3 placePosition = target.blockPosition + target.hitNormal;
                            world.placeBlock(placePosition, selectedItem.blockID, &renderer);
                        }
                    } else if (selectedItem.type == InventoryItemType::STRUCTURE) {
                        // Place structure at ground level below targeted position
                        // Convert target block position to block coordinates
                        glm::ivec3 targetBlockCoords(
                            (int)(target.blockPosition.x),
                            (int)(target.blockPosition.y),
                            (int)(target.blockPosition.z)
                        );

                        // Find ground level by casting ray downward
                        glm::vec3 downDirection(0.0f, -1.0f, 0.0f);
                        RaycastHit groundHit = Raycast::castRay(&world, target.blockPosition, downDirection, 256.0f);

                        glm::ivec3 spawnPos;
                        if (groundHit.hit) {
                            // Found solid ground below, place structure on top of it
                            spawnPos = glm::ivec3(groundHit.blockX, groundHit.blockY, groundHit.blockZ);
                            spawnPos.y += 1;  // One block above ground
                        } else {
                            // No ground found below, place at target level
                            spawnPos = targetBlockCoords;
                        }

                        // Spawn the structure
                        StructureRegistry::instance().spawnStructure(selectedItem.structureName, &world, spawnPos, &renderer);
                    }
                }
            } else {
                rightMousePressed = false;
            }

            // Update water simulation (OPTIMIZED with dirty list + chunk freezing)
            // Only updates water cells that changed (dirty list tracking)
            // Only simulates water within render distance (chunk freezing)
            // Performance: O(dirty_cells_in_range) instead of O(all_chunks)
            static float liquidUpdateTimer = 0.0f;
            liquidUpdateTimer += deltaTime;
            const float liquidUpdateInterval = 0.1f;  // Update liquids 10 times per second for smooth flow
            if (liquidUpdateTimer >= liquidUpdateInterval) {
                liquidUpdateTimer = 0.0f;
                world.updateWaterSimulation(deltaTime, &renderer, player.Position, renderDistance);
            }

            // Begin rendering
            if (!renderer.beginFrame()) {
                // Skip this frame (swap chain recreation in progress)
                continue;
            }

            // Get current descriptor set (need to store it to take address)
            VkDescriptorSet currentDescriptorSet = renderer.getCurrentDescriptorSet();

            // Render skybox first (renders behind everything)
            renderer.renderSkybox();

            // Render world with normal or wireframe pipeline based on debug mode
            VkPipeline worldPipeline = DebugState::instance().wireframeMode.getValue() ?
                renderer.getWireframePipeline() : renderer.getGraphicsPipeline();
            vkCmdBindPipeline(renderer.getCurrentCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, worldPipeline);
            vkCmdBindDescriptorSets(renderer.getCurrentCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                                   renderer.getPipelineLayout(), 0, 1, &currentDescriptorSet, 0, nullptr);
            world.renderWorld(renderer.getCurrentCommandBuffer(), player.Position, viewProj, renderDistance, &renderer);

            // Render block outline with line pipeline
            if (target.hasTarget) {
                vkCmdBindPipeline(renderer.getCurrentCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, renderer.getLinePipeline());
                vkCmdBindDescriptorSets(renderer.getCurrentCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                                       renderer.getPipelineLayout(), 0, 1, &currentDescriptorSet, 0, nullptr);
                targetingSystem.renderBlockOutline(renderer.getCurrentCommandBuffer());
            }

            // Render ImGui (crosshair when playing, menu when paused, console overlay)
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            if (isPaused) {
                if (pauseMenu.render()) {
                    isPaused = false;
                    requestMouseReset = true;
                }
            } else if (!console.isVisible() && !inventory.isOpen()) {
                targetingSystem.renderCrosshair();
            }

            // Render console (overlays on top of everything)
            console.render();

            // Render inventory UI (full inventory when open, hotbar always visible)
            if (inventory.isOpen()) {
                inventory.render(&renderer);
            }
            if (!isPaused && !console.isVisible()) {
                inventory.renderHotbar(&renderer);
                inventory.renderSelectedBlockPreview(&renderer);
            }

            // Render FPS counter if enabled
            if (DebugState::instance().drawFPS.getValue()) {
                ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
                ImGui::SetNextWindowBgAlpha(0.5f);
                ImGui::Begin("FPS", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
                ImGui::Text("FPS: %.1f", DebugState::instance().lastFPS);
                ImGui::End();
            }

            // Render debug info if enabled
            if (DebugState::instance().renderDebug.getValue()) {
                ImGui::SetNextWindowPos(ImVec2(10, 50), ImGuiCond_Always);
                ImGui::SetNextWindowBgAlpha(0.5f);
                ImGui::Begin("Debug", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
                ImGui::Text("Position: (%.1f, %.1f, %.1f)", player.Position.x, player.Position.y, player.Position.z);
                ImGui::Text("Noclip: %s", player.NoclipMode ? "ON" : "OFF");
                ImGui::End();
            }

            // Render target info if enabled (opposite corner from FPS)
            if (DebugState::instance().showTargetInfo.getValue()) {
                ImGuiIO& io = ImGui::GetIO();
                ImVec2 displaySize = io.DisplaySize;
                ImGui::SetNextWindowPos(ImVec2(displaySize.x - 200, 10), ImGuiCond_Always);
                ImGui::SetNextWindowBgAlpha(0.5f);
                ImGui::Begin("Target Info", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
                if (target.hasTarget) {
                    ImGui::Text("=== Target Info ===");
                    ImGui::Text("Block: %s", target.blockName.c_str());
                    ImGui::Text("Type: %s", target.blockType.c_str());
                    ImGui::Text("Position: (%d, %d, %d)", target.blockCoords.x, target.blockCoords.y, target.blockCoords.z);
                    ImGui::Text("Distance: %.1fm", target.distance);
                    ImGui::Text("Breakable: %s", target.isBreakable ? "Yes" : "No");
                } else {
                    ImGui::Text("=== Target Info ===");
                    ImGui::Text("No target");
                }
                ImGui::End();
            }

            // Render culling stats if enabled
            if (DebugState::instance().showCullingStats.getValue()) {
                ImGui::SetNextWindowPos(ImVec2(10, 110), ImGuiCond_Always);
                ImGui::SetNextWindowBgAlpha(0.5f);
                ImGui::Begin("Culling Stats", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
                ImGui::Text("=== Chunk Culling ===");
                ImGui::Text("Rendered: %d", DebugState::instance().chunksRendered);
                ImGui::Text("Distance Culled: %d", DebugState::instance().chunksDistanceCulled);
                ImGui::Text("Frustum Culled: %d", DebugState::instance().chunksFrustumCulled);
                ImGui::Text("Total in World: %d", DebugState::instance().chunksTotalInWorld);

                // Calculate culling efficiency percentage
                int totalCulled = DebugState::instance().chunksDistanceCulled + DebugState::instance().chunksFrustumCulled;
                int totalChunks = DebugState::instance().chunksTotalInWorld;
                float cullingPercent = (totalChunks > 0) ? (totalCulled * 100.0f / totalChunks) : 0.0f;
                ImGui::Text("Culled: %.1f%%", cullingPercent);
                ImGui::End();
            }

            ImGui::Render();
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), renderer.getCurrentCommandBuffer());

            // End rendering
            renderer.endFrame();
        }

        std::cout << "Shutting down..." << std::endl;

        // Wait for device to finish before cleanup
        std::cout << "  Waiting for GPU to finish..." << std::endl;
        vkDeviceWaitIdle(renderer.getDevice());
        std::cout << "  GPU idle" << std::endl;

        // Save world, player, and inventory before cleanup
        std::cout << "  Saving world..." << std::endl;
        std::string worldPath = "worlds/world_" + std::to_string(seed);
        if (world.saveWorld(worldPath)) {
            std::cout << "  World saved successfully to " << worldPath << std::endl;
        } else {
            std::cout << "  Warning: Failed to save world" << std::endl;
        }

        std::cout << "  Saving player state..." << std::endl;
        if (player.savePlayerState(worldPath)) {
            std::cout << "  Player state saved successfully" << std::endl;
        } else {
            std::cout << "  Warning: Failed to save player state" << std::endl;
        }

        std::cout << "  Saving inventory..." << std::endl;
        if (inventory.save(worldPath)) {
            std::cout << "  Inventory saved successfully" << std::endl;
        } else {
            std::cout << "  Warning: Failed to save inventory" << std::endl;
        }

        // Cleanup world chunk buffers
        std::cout << "  Cleaning up world..." << std::endl;
        world.cleanup(&renderer);
        std::cout << "  World cleanup complete" << std::endl;

        // Cleanup ImGui
        std::cout << "  Cleaning up ImGui..." << std::endl;
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        vkDestroyDescriptorPool(renderer.getDevice(), imguiPool, nullptr);
        std::cout << "  ImGui cleanup complete" << std::endl;

        // Save current window size to config
        std::cout << "  Saving config..." << std::endl;
        int currentWidth, currentHeight;
        glfwGetWindowSize(window, &currentWidth, &currentHeight);
        config.setInt("Window", "width", currentWidth);
        config.setInt("Window", "height", currentHeight);
        config.saveToFile("config.ini");
        std::cout << "  Config saved" << std::endl;

        std::cout << "  Destroying window..." << std::endl;
        glfwDestroyWindow(window);
        std::cout << "  Terminating GLFW..." << std::endl;
        glfwTerminate();
        std::cout << "  Cleaning up noise..." << std::endl;
        Chunk::cleanupNoise();

        std::cout << "Shutdown complete." << std::endl;
        std::cout << "Exiting main()..." << std::endl;
        std::cout.flush();  // Force flush before destructors run
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return -1;
    }

    std::cout << "After exception handling" << std::endl;
    std::cout.flush();
}
