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
#include <chrono>
#include <future>
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
#include "world_streaming.h"
#include "lighting_system.h"
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
#include "perf_monitor.h"
#include "targeting_system.h"
#include "raycast.h"
#include "input_manager.h"
#include "inventory.h"
#include "terrain_constants.h"
#include "sun_tracker.h"
#include "frustum.h"
#include "mesh/mesh_renderer.h"
#include "mesh/mesh_loader.h"
#include "map_preview.h"
#include "loading_sphere.h"
#include "event_dispatcher.h"
// BlockIconRenderer is now part of block_system.h

// Game state
enum class GameState {
    MAIN_MENU,
    IN_GAME
};

// Global variables
VulkanRenderer* g_renderer = nullptr;
Inventory* g_inventory = nullptr;
bool g_debugMode = false;  // Quick-start mode for development (-debug flag)

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
    std::cerr << "[vulkan] Error: VkResult = " << err << '\n';
    if (err < 0)
        abort();
}

int main(int argc, char* argv[]) {
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-debug" || arg == "--debug") {
            g_debugMode = true;
            std::cout << "=== DEBUG MODE ENABLED ===" << '\n';
            std::cout << "Skipping main menu, using reduced world size for quick iteration" << '\n';
            std::cout << "=========================" << '\n';
        }
    }

    try {
        // Load configuration first
        Config& config = Config::instance();
        if (!config.loadFromFile("config.ini")) {
            std::cerr << "Warning: Failed to load config.ini, using default values" << '\n';
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
        std::cout << "Initializing Vulkan renderer..." << '\n';
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

        // ========== EVENT SYSTEM INITIALIZATION ==========
        // Start the event dispatcher thread for async event processing
        std::cout << "Starting event dispatcher..." << '\n';
        EventDispatcher::instance().start();

        // ========== LOADING SCREEN SYSTEM ==========
        // Thread-safe loading state (atomic for cross-thread access)
        std::atomic<float> loadingProgress{0.0f};
        std::atomic<bool> loadingComplete{false};
        std::atomic<bool> loadingThreadRunning{false};
        std::string loadingMessage = "Initializing";
        std::mutex loadingMessageMutex;
        std::mutex renderMutex;  // Protects Vulkan rendering operations

        // Map preview for loading screen (shows terrain as it generates)
        MapPreview* mapPreview = nullptr;

        // Spinning 3D sphere for loading screen (prevents "frozen" appearance)
        LoadingSphere loadingSphere;
        bool sphereInitialized = loadingSphere.initialize(&renderer);
        if (sphereInitialized) {
            std::cout << "Loading sphere initialized successfully" << '\n';
        }

        // ========== THREADED LOADING SCREEN RENDERER ==========
        // Runs continuously on its own thread to keep sphere spinning during loading
        auto loadingRenderThread = [&]() {
            int dotAnimationFrame = 0;

            while (loadingThreadRunning.load() && !loadingComplete.load()) {
                // Try to acquire render lock - skip frame if main thread is doing Vulkan ops
                std::unique_lock<std::mutex> renderLock(renderMutex, std::try_to_lock);
                if (!renderLock.owns_lock()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    continue;
                }

                if (!renderer.beginFrame()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(16));
                    continue;
                }

                // Get current message safely
                std::string currentMessage;
                {
                    std::lock_guard<std::mutex> lock(loadingMessageMutex);
                    currentMessage = loadingMessage;
                }

                // Animate dots pattern
                const char* dotPatterns[] = {".", "..", "...", ".", "..", "..."};
                std::string animatedMessage = currentMessage + dotPatterns[dotAnimationFrame % 6];
                dotAnimationFrame++;

                // Update map preview texture if available
                if (mapPreview && mapPreview->isReady()) {
                    mapPreview->updateTexture();
                }

                // ========== RENDER SPINNING SPHERE (before ImGui overlay) ==========
                if (sphereInitialized && loadingSphere.isReady()) {
                    loadingSphere.render();
                }

                // Start ImGui frame
                ImGui_ImplVulkan_NewFrame();
                ImGui_ImplGlfw_NewFrame();
                ImGui::NewFrame();

                // Full-screen semi-transparent overlay (sphere visible through)
                ImGuiIO& io = ImGui::GetIO();
                ImGui::SetNextWindowPos(ImVec2(0, 0));
                ImGui::SetNextWindowSize(io.DisplaySize);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.7f));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                ImGui::Begin("LoadingOverlay", nullptr,
                    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs);

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
                ImGui::ProgressBar(loadingProgress.load(), ImVec2(300, 30), "");
                ImGui::PopStyleColor();

                // Percentage text
                ImGui::SetCursorPos(ImVec2(centerX - 30, centerY + 40));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
                ImGui::Text("%.0f%%", loadingProgress.load() * 100.0f);
                ImGui::PopStyleColor();

                // Label
                ImGui::SetCursorPos(ImVec2(centerX - 80, centerY + 70));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                ImGui::Text("Generating world...");
                ImGui::PopStyleColor();

                ImGui::End();
                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor();

                ImGui::Render();
                ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), renderer.getCurrentCommandBuffer());

                renderer.endFrame();
                glfwPollEvents();

                // Target ~60fps for smooth animation
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }
        };

        // For Vulkan operations during loading, acquire render mutex
        auto renderLoadingScreen = [&]() {
            if (loadingComplete.load()) return;
            glfwPollEvents();  // Keep window responsive
        };

        // ========== MAIN GAME LOOP ==========
        // Outer loop allows cycling: Main Menu -> Game -> Main Menu -> Game -> ...
        // Only exits when player selects Quit or closes window
        std::cout << "Entering main game loop..." << '\n';
        MainMenu mainMenu(window);
        GameState gameState = GameState::MAIN_MENU;
        int seed = config.getInt("World", "seed", 1124345);  // Default seed
        bool shouldQuit = false;
        MenuResult menuResult;  // Declared here so it's accessible in both menu and game blocks

        // DEBUG MODE: Skip main menu, use default settings for quick iteration
        if (g_debugMode) {
            std::cout << "Debug mode: Skipping main menu, generating small test world..." << '\n';
            gameState = GameState::IN_GAME;
            seed = 12345;  // Fixed debug seed for reproducibility
            menuResult.action = MenuAction::NEW_GAME;
            menuResult.seed = seed;
            menuResult.temperatureBias = 0.0f;  // No biome modifiers
            menuResult.moistureBias = 0.0f;
            menuResult.ageBias = 0.0f;
            menuResult.worldPath = "";
        }

        while (!shouldQuit && !glfwWindowShouldClose(window)) {
            if (gameState == GameState::MAIN_MENU) {
                // ========== MAIN MENU ==========
                std::cout << "Showing main menu..." << '\n';

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
                    menuResult = mainMenu.render();

                    // Process menu result
                    if (menuResult.action == MenuAction::NEW_GAME) {
                        seed = menuResult.seed;
                        gameState = GameState::IN_GAME;
                        std::cout << "Starting new game with seed: " << seed << '\n';
                    } else if (menuResult.action == MenuAction::LOAD_GAME) {
                        if (!menuResult.worldPath.empty()) {
                            // Set flag to load world instead of generating new one
                            gameState = GameState::IN_GAME;
                            std::cout << "Loading world from: " << menuResult.worldPath << '\n';
                        } else {
                            std::cout << "Error: No world path provided" << '\n';
                        }
                    } else if (menuResult.action == MenuAction::QUIT) {
                        shouldQuit = true;
                        break;
                    }

                    // Render ImGui
                    ImGui::Render();
                    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), renderer.getCurrentCommandBuffer());

                    renderer.endFrame();
                }

                // If player quit from menu, break outer loop to shutdown
                if (shouldQuit || glfwWindowShouldClose(window)) {
                    std::cout << "Player quit from main menu" << '\n';
                    break;
                }
            }

            // If we're quitting, break to shutdown
            if (shouldQuit || glfwWindowShouldClose(window)) {
                break;
            }

            // Only proceed to game if we're in IN_GAME state
            if (gameState == GameState::IN_GAME) {
                // ========== GAME INITIALIZATION ==========
                // Only runs when starting a new game or loading a world
                std::cout << "Initializing game..." << '\n';

            // CRITICAL FIX: Reset loading screen flag for subsequent world loads
            // Without this, the loading screen won't show on second+ world generation
            loadingComplete.store(false);
            loadingProgress.store(0.0f);

            // Reset sphere animation timer for new load
            loadingSphere.resetTimer();

            // ========== START LOADING RENDER THREAD ==========
            // Render thread runs continuously to keep sphere spinning during loading
            loadingThreadRunning.store(true);
            std::thread renderThread(loadingRenderThread);

            // Disable cursor for gameplay
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

        // Determine if we're loading an existing world or creating a new one
        bool loadingExistingWorld = (menuResult.action == MenuAction::LOAD_GAME && !menuResult.worldPath.empty());
        std::string worldPath = menuResult.worldPath;

        // World dimensions are no longer used - infinite world via streaming
        // These dummy values are kept for World constructor compatibility
        int worldWidth = 1;
        int worldHeight = 1;
        int worldDepth = 1;

        // Loading stages 1-3: Parallel asset loading (10-20%)
        // OPTIMIZATION: Load all registries in parallel (3x faster startup)
        loadingProgress.store(0.05f);
        { std::lock_guard<std::mutex> lock(loadingMessageMutex); loadingMessage = "Loading assets"; }
        renderLoadingScreen();
        std::cout << "Loading all registries in parallel..." << '\n';

        // Launch parallel loading tasks
        auto blockLoadFuture = std::async(std::launch::async, [&renderer]() {
            std::cout << "  [Thread] Loading block registry..." << '\n';
            BlockRegistry::instance().loadBlocks("assets/blocks", &renderer);
            std::cout << "  [Thread] Block registry loaded!" << '\n';
        });

        auto structureLoadFuture = std::async(std::launch::async, []() {
            std::cout << "  [Thread] Loading structure registry..." << '\n';
            StructureRegistry::instance().loadStructures("assets/structures");
            std::cout << "  [Thread] Structure registry loaded!" << '\n';
        });

        auto biomeLoadFuture = std::async(std::launch::async, []() {
            std::cout << "  [Thread] Loading biome registry..." << '\n';
            BiomeRegistry::getInstance().loadBiomes("assets/biomes");
            std::cout << "  [Thread] Biome registry loaded!" << '\n';
        });

        // Wait for all registries to load (blocks takes longest ~500ms)
        blockLoadFuture.get();
        structureLoadFuture.get();
        biomeLoadFuture.get();
        std::cout << "All registries loaded successfully!" << '\n';

        // Loading stage 4: Bind textures (25%)
        loadingProgress.store(0.25f);
        { std::lock_guard<std::mutex> lock(loadingMessageMutex); loadingMessage = "Setting up renderer"; }
        renderLoadingScreen();
        std::cout << "Binding texture atlas..." << '\n';
        renderer.bindAtlasTexture(
            BlockRegistry::instance().getAtlasImageView(),
            BlockRegistry::instance().getAtlasSampler()
        );

        // Create ImGui descriptor set for the texture atlas (for inventory icons)
        std::cout << "Creating ImGui atlas descriptor..." << '\n';
        VkDescriptorSet atlasImGuiDescriptor = ImGui_ImplVulkan_AddTexture(
            BlockRegistry::instance().getAtlasSampler(),
            BlockRegistry::instance().getAtlasImageView(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        );
        BlockIconRenderer::init(atlasImGuiDescriptor);

        // Loading stage 5: Initialize world (30%)
        loadingProgress.store(0.30f);
        { std::lock_guard<std::mutex> lock(loadingMessageMutex); loadingMessage = loadingExistingWorld ? "Loading world data" : "Initializing world generator"; }
        renderLoadingScreen();

        World world(worldWidth, worldHeight, worldDepth, seed,
                    menuResult.temperatureBias, menuResult.moistureBias, menuResult.ageBias);

        if (loadingExistingWorld) {
            std::cout << "Loading world from: " << worldPath << '\n';
            Chunk::initNoise(seed);  // Init with placeholder, will be overwritten by loaded seed

            loadingProgress.store(0.35f);
            { std::lock_guard<std::mutex> lock(loadingMessageMutex); loadingMessage = "Loading chunks from disk"; }
            renderLoadingScreen();

            if (!world.loadWorld(worldPath)) {
                std::cerr << "Failed to load world, falling back to new world generation" << '\n';
                loadingExistingWorld = false;
            } else {
                // Update seed from loaded world
                seed = world.getSeed();
                Chunk::initNoise(seed);  // Re-init with correct seed

                auto& chunks = world.getChunks();
                std::cout << "Loaded " << chunks.size() << " chunks from disk" << '\n';

                // PERFORMANCE FIX: Initialize lighting BEFORE generating meshes
                // This eliminates wasted mesh generation with incorrect/zero lighting
                // Old flow: mesh → lighting → mesh regeneration (DOUBLE WORK)
                // New flow: lighting → mesh (SINGLE PASS)

                loadingProgress.store(0.50f);
                { std::lock_guard<std::mutex> lock(loadingMessageMutex); loadingMessage = "Initializing lighting"; }
                renderLoadingScreen();
                std::cout << "Initializing lighting for loaded chunks..." << '\n';

                // Initialize chunk lighting (vertical sunlight pass)
                for (Chunk* chunk : chunks) {
                    world.initializeChunkLighting(chunk);
                }

                // Complete light propagation (horizontal BFS)
                loadingProgress.store(0.60f);
                { std::lock_guard<std::mutex> lock(loadingMessageMutex); loadingMessage = "Initializing lighting"; }
                renderLoadingScreen();
                std::cout << "Initializing block lights (torches, lava)..." << '\n';

                // PERFORMANCE: Skip sky light initialization - we use heightmaps instead!
                // Old system: BFS propagation through 100M+ air blocks (3-5 seconds wasted)
                // New system: Heightmap calculates sky light in O(1) during mesh generation
                // We only need to initialize block lights (torches, lava) if any exist

                // NOTE: initializeWorldLighting() is disabled because:
                // 1. Sky light: Calculated via heightmap (100x+ faster)
                // 2. Block lights: Not needed during initial load (no torches placed yet)
                // 3. Lighting updates: Handled incrementally during gameplay

                // Uncomment only if you need to initialize block lights from saved data:
                // world.getLightingSystem()->initializeWorldLighting([&](float progress) {
                //     loadingProgress = 0.60f + (progress * 0.10f);
                //     renderLoadingScreen();
                // });

                std::cout << "Lighting initialization skipped (using heightmap system)" << '\n';

                // NOW generate meshes WITH correct lighting (single pass)
                loadingProgress.store(0.70f);
                { std::lock_guard<std::mutex> lock(loadingMessageMutex); loadingMessage = "Building chunk meshes"; }
                renderLoadingScreen();
                std::cout << "Generating meshes with lighting for " << chunks.size() << " chunks..." << '\n';

                // Parallel mesh generation for better performance
                unsigned int numThreads = std::thread::hardware_concurrency();
                if (numThreads == 0) numThreads = 4;
                std::vector<std::thread> threads;
                threads.reserve(numThreads);

                size_t chunksPerThread = (chunks.size() + numThreads - 1) / numThreads;

                for (unsigned int i = 0; i < numThreads; ++i) {
                    size_t startIdx = i * chunksPerThread;
                    size_t endIdx = std::min(startIdx + chunksPerThread, chunks.size());

                    if (startIdx >= chunks.size()) break;

                    threads.emplace_back([&world, &chunks, startIdx, endIdx]() {
                        for (size_t idx = startIdx; idx < endIdx; ++idx) {
                            chunks[idx]->generateMesh(&world);
                        }
                    });
                }

                // Wait for all threads to complete
                for (auto& thread : threads) {
                    thread.join();
                }

                std::cout << "Generated " << chunks.size() << " chunk meshes with correct lighting!" << '\n';
            }
        }

        if (!loadingExistingWorld) {
            std::cout << "Initializing world generation..." << '\n';
            Chunk::initNoise(seed);

            // Set world path for new worlds (enables save-on-unload for chunk streaming)
            std::string newWorldPath = "worlds/world_" + std::to_string(seed);
            world.saveWorld(newWorldPath);  // Creates directory and saves initial metadata
            std::cout << "World path set to: " << newWorldPath << '\n';

            // ============================================================================
            // LIVE MAP PREVIEW (2025-11-25): Initialize preview before chunk generation
            // ============================================================================
            loadingProgress.store(0.33f);
            { std::lock_guard<std::mutex> lock(loadingMessageMutex); loadingMessage = "Creating map preview"; }
            renderLoadingScreen();

            mapPreview = new MapPreview();
            if (mapPreview->initialize(world.getBiomeMap(), &renderer, 0, 0)) {
                std::cout << "Generating map preview..." << '\n';
                mapPreview->generateFullPreview();
                mapPreview->updateTexture();

                // Connect map preview to loading sphere so it shows the world texture
                loadingSphere.setMapPreview(mapPreview);
            }

            // Loading stage 6: Generate spawn area only (much faster than full world)
            // With 320 chunk height, generating all 46,080 chunks takes forever
            // Instead, generate just a small area around spawn and let streaming handle the rest
            loadingProgress.store(0.35f);
            { std::lock_guard<std::mutex> lock(loadingMessageMutex); loadingMessage = "Generating spawn area"; }
            renderLoadingScreen();
            std::cout << "Generating spawn chunks (streaming will handle the rest)..." << '\n';

            // We'll determine spawn chunk coordinates and generate them before finding exact spawn point
            // Assume spawn will be somewhere near (0, 64, 0) in world space
            // Generate spawn chunks around origin
            int spawnChunkX = 0;
            int spawnChunkY = 2;  // Y=64 surface is in chunk Y=2
            int spawnChunkZ = 0;

            // MINECRAFT-STYLE SPAWN GENERATION (2025-11-25)
            // Minecraft generates ~19×19 chunk "spawn chunks" that stay loaded
            // We generate: inner=6 (decorated), outer=12 (terrain buffer)
            // This creates a solid starting area with pre-loaded terrain buffer
            // DEBUG MODE: Use smaller radius (3 instead of 6) for faster iteration
            const int SPAWN_RADIUS = g_debugMode ? 3 : 6;
            int spawnRadius = SPAWN_RADIUS;

            std::cout << "Generating " << spawnRadius << " chunk radius ("
                     << ((2*spawnRadius+1)*(2*spawnRadius+1)*(2*spawnRadius+1))
                     << " chunks) to fully cover load sphere..." << '\n';

            world.generateSpawnChunks(spawnChunkX, spawnChunkY, spawnChunkZ, spawnRadius);

            // Decorate world with trees and features
            // Only decorates spawn chunks for faster startup
            std::cout << "Placing trees and features..." << '\n';
            world.decorateWorld();

            // PERFORMANCE FIX (2025-11-23): Skip bulk water registration at startup
            // Water is already registered incrementally in addStreamedChunk() (world.cpp:1144)
            // Bulk scanning all chunks at startup causes freeze on large worlds
            // std::cout << "Initializing water physics..." << '\n';
            // world.registerWaterBlocks();

            // PERFORMANCE: Skip lighting initialization - using heightmap system
            // Heightmap calculates sky light instantly during mesh generation
            loadingProgress.store(0.75f);
            { std::lock_guard<std::mutex> lock(loadingMessageMutex); loadingMessage = "Preparing lighting"; }
            renderLoadingScreen();
            std::cout << "Lighting ready (heightmap-based)" << '\n';

            // DISABLED: BFS lighting initialization (wasted with heightmap system)
            // world.getLightingSystem()->initializeWorldLighting([&](float progress) {
            //     loadingProgress = 0.75f + (progress * 0.02f);
            //     renderLoadingScreen();
            // });

            // Regenerate all meshes with updated lighting (synchronously during loading)
            // Pass nullptr for renderer to skip GPU upload (createBuffers will batch upload later)
            loadingProgress.store(0.77f);
            { std::lock_guard<std::mutex> lock(loadingMessageMutex); loadingMessage = "Updating lighting on meshes"; }
            renderLoadingScreen();
            std::cout << "Regenerating meshes with final lighting..." << '\n';
            world.getLightingSystem()->regenerateAllDirtyChunks(10000, nullptr);  // Mesh only, no GPU upload yet
            std::cout << "Mesh regeneration complete!" << '\n';
        }

        // Loading stage 8: Create GPU buffers (85%)
        loadingProgress.store(0.80f);
        { std::lock_guard<std::mutex> lock(loadingMessageMutex); loadingMessage = "Creating GPU buffers"; }
        renderLoadingScreen();
        std::cout << "Creating GPU buffers..." << '\n';
        world.createBuffers(&renderer);

        // Loading stage 8.5: GPU warm-up - wait for all uploads to finish (87%)
        loadingProgress.store(0.87f);
        { std::lock_guard<std::mutex> lock(loadingMessageMutex); loadingMessage = "Warming up GPU (this ensures smooth 60 FPS)"; }
        renderLoadingScreen();
        std::cout << "Warming up GPU - waiting for all chunk uploads to complete..." << '\n';
        renderer.waitForGPUIdle();
        std::cout << "GPU warm-up complete - ready for 60 FPS gameplay!" << '\n';

        // Loading stage 9: Finding spawn location (90%)
        loadingProgress.store(0.88f);
        { std::lock_guard<std::mutex> lock(loadingMessageMutex); loadingMessage = "Finding safe spawn location"; }
        renderLoadingScreen();

        // Improved spawn logic: Search for safe flat area with solid ground (no caves below)
        float spawnX = 0.0f;
        float spawnZ = 0.0f;
        int spawnGroundY = -1;

        std::cout << "Finding safe spawn location..." << '\n';

        // World is centered around Y=0 with both positive and negative chunks
        // Search for spawn on surface
        const int MAX_TERRAIN_HEIGHT = 180;  // Search up to Y=180
        const int MIN_SEARCH_Y = 10;  // Don't spawn below Y=10
        const int SEARCH_RADIUS = 32;  // Search in 32 block radius
        const int MIN_SOLID_DEPTH = 5;  // Require at least 5 blocks of solid ground below

        std::cout << "Searching for spawn from Y=" << MAX_TERRAIN_HEIGHT << " down to Y=" << MIN_SEARCH_Y << '\n';

        // Helper to check if a location is safe to spawn
        // SPAWN SAFETY CHECKS (2025-11-25):
        // 1. Not in/under water
        // 2. Not in a cave (no large air pockets below)
        // 3. Not on ice (slippery)
        // 4. Clear headroom above
        auto isSafeSpawn = [&world, MIN_SOLID_DEPTH, MAX_TERRAIN_HEIGHT](float x, float z, int groundY) -> bool {
            // Bounds check
            if (groundY < MIN_SOLID_DEPTH || groundY >= MAX_TERRAIN_HEIGHT - 4) {
                return false;  // Too extreme for spawn
            }

            // Check the standing block - must be solid, not water/ice
            int standingBlock = world.getBlockAt(x, static_cast<float>(groundY), z);
            if (standingBlock == 0 || standingBlock == TerrainGeneration::BLOCK_WATER ||
                standingBlock == TerrainGeneration::BLOCK_ICE) {
                return false;  // Can't spawn on water, ice, or air
            }

            // Check if there's solid ground below (no caves)
            for (int dy = 0; dy < MIN_SOLID_DEPTH; dy++) {
                int blockID = world.getBlockAt(x, static_cast<float>(groundY - dy), z);
                // Must be solid (not air=0, not water=5)
                if (blockID == 0 || blockID == TerrainGeneration::BLOCK_WATER) {
                    return false;  // Cave or water underneath
                }
            }

            // Check for high cave ceilings (reject if there's a cave ceiling far above)
            // This prevents spawning in caves with tall ceilings that feel underground
            int airBlocksAbove = 0;
            for (int dy = 1; dy <= 20; dy++) {
                int blockID = world.getBlockAt(x, static_cast<float>(groundY + dy), z);
                if (blockID == 0) {
                    airBlocksAbove++;
                } else {
                    // Found a solid block above - this might be a cave ceiling
                    // If it's very high (10+ blocks), we're probably in a big cave
                    if (airBlocksAbove >= 10 && dy <= 15) {
                        return false;  // Cave with high ceiling
                    }
                    break;  // Found ceiling, stop checking
                }
            }

            // Check if there's clear space above for player (2 blocks tall minimum)
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
                                          << ") with solid ground below (block ID: " << currentBlock << ")" << '\n';
                                break;
                            }
                        }
                    }
                }
            }
        }

        // Emergency fallback if no safe spawn found
        if (spawnGroundY < 0) {
            std::cout << "WARNING: No safe spawn found in initial search, validating fallback at (0, 0, 64)" << '\n';

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
                        std::cout << "Fallback spawn validated at (0, " << y << ", 0)" << '\n';
                        break;
                    }
                }
            }

            // If fallback at (0,0) also failed, do expanding radius search with relaxed constraints
            if (!foundFallback) {
                std::cout << "WARNING: Fallback at (0,0) failed, searching in expanding radius..." << '\n';
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
                                        std::cout << "Emergency spawn found at (" << testX << ", " << y << ", " << testZ << ")" << '\n';
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
                              << emergencyY << " at (0, 0)" << '\n';
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
        std::cout << "Blocks at spawn location:" << '\n';
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
            std::cout << marker << '\n';
        }

        std::cout << "Spawn at (" << spawnX << ", " << spawnY << ", " << spawnZ
                  << ") - surface Y=" << spawnGroundY << '\n';

        // CRITICAL DEBUG: Verify blocks exist where we think they do
        std::cout << "\n=== SPAWN VERIFICATION ===" << '\n';
        float feetY = spawnY - 1.6f;
        std::cout << "Player feet will be at Y=" << feetY << " (in block " << static_cast<int>(std::floor(feetY)) << ")" << '\n';
        int groundBlock = world.getBlockAt(spawnX, static_cast<float>(spawnGroundY), spawnZ);
        int feetBlock = world.getBlockAt(spawnX, feetY, spawnZ);
        std::cout << "Block at ground Y=" << spawnGroundY << ": blockID=" << groundBlock << (groundBlock != 0 ? " ✓ SOLID" : " ✗ AIR!") << '\n';
        std::cout << "Block at feet Y=" << static_cast<int>(std::floor(feetY)) << ": blockID=" << feetBlock << (feetBlock == 0 ? " ✓ AIR" : " ✗ SOLID!") << '\n';

        bool spawnValid = (groundBlock != 0) && (feetBlock == 0);
        if (!spawnValid) {
            std::cout << "ERROR: Spawn validation FAILED!" << '\n';
            if (groundBlock == 0) {
                std::cout << "  - Ground block is AIR (expected SOLID)" << '\n';
            }
            if (feetBlock != 0) {
                std::cout << "  - Feet position is SOLID (expected AIR)" << '\n';
            }
        } else {
            std::cout << "Spawn validation PASSED ✓" << '\n';
        }
        std::cout << "===========================\n" << '\n';

        // Loading stage 10: Spawning player (95%)
        loadingProgress.store(0.95f);
        { std::lock_guard<std::mutex> lock(loadingMessageMutex); loadingMessage = "Spawning player"; }
        renderLoadingScreen();
        Player player(glm::vec3(spawnX, spawnY, spawnZ), glm::vec3(0.0f, 1.0f, 0.0f), -90.0f, 0.0f);

        // Loading stage 11: Initializing game systems (98%)
        loadingProgress.store(0.98f);
        { std::lock_guard<std::mutex> lock(loadingMessageMutex); loadingMessage = "Initializing game systems"; }
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

        // Loading stage 12: Final check - wait for player to be on ground (99%)
        // NOTE: Lighting initialization moved earlier (before GPU upload) to prevent chunk re-uploads
        loadingProgress.store(0.99f);
        { std::lock_guard<std::mutex> lock(loadingMessageMutex); loadingMessage = "Ready"; }
        renderLoadingScreen();

        // Verify player is safely on the ground before hiding loading screen
        // Update physics once to ensure player settles (disable input during this)
        player.update(window, 0.016f, &world, false);  // 1 frame at 60fps, no input

        // Final loading screen frame at 100%
        loadingProgress.store(1.0f);
        { std::lock_guard<std::mutex> lock(loadingMessageMutex); loadingMessage = "Ready"; }

        // ========== STOP LOADING RENDER THREAD ==========
        loadingComplete.store(true);
        loadingThreadRunning.store(false);
        if (renderThread.joinable()) {
            renderThread.join();
        }

        // Cleanup map preview (no longer needed after loading)
        if (mapPreview) {
            mapPreview->cleanup();
            delete mapPreview;
            mapPreview = nullptr;
        }

        // Initialize world streaming for infinite world generation
        std::cout << "Starting world streaming system..." << '\n';
        WorldStreaming worldStreaming(&world, world.getBiomeMap(), &renderer);

        // SPAWN ANCHOR (2025-11-25): Keep spawn chunks permanently loaded (like Minecraft)
        // Uses same radius as spawn generation - these chunks never unload
        // DEBUG MODE: Use smaller anchor (3 instead of 6) for faster iteration
        const int ANCHOR_RADIUS = g_debugMode ? 3 : 6;
        worldStreaming.setSpawnAnchor(0, 2, 0, ANCHOR_RADIUS);  // chunk (0,2,0) = world origin surface

        worldStreaming.start();  // Starts worker threads (default: CPU cores - 1)

        // Initialize mesh rendering system
        std::cout << "Initializing mesh rendering system..." << '\n';
        MeshRenderer meshRenderer(&renderer);

        // Create test meshes near spawn
        std::cout << "Creating test meshes..." << '\n';

        // Test 1: Red cube near spawn
        Mesh cube1 = MeshLoader::createCube(2.0f);
        uint32_t cubeMeshId = meshRenderer.createMesh(cube1);

        PBRMaterial redMaterial = PBRMaterial::createDefault();
        redMaterial.baseColor = glm::vec4(1.0f, 0.2f, 0.2f, 1.0f);
        redMaterial.metallic = 0.0f;
        redMaterial.roughness = 0.6f;
        uint32_t redMatId = meshRenderer.createMaterial(redMaterial);
        meshRenderer.setMeshMaterial(cubeMeshId, redMatId);

        // Create instance 10 blocks in front of spawn
        glm::mat4 cubeTransform = glm::translate(glm::mat4(1.0f),
                                                 glm::vec3(spawnX + 10.0f, spawnY, spawnZ));
        meshRenderer.createInstance(cubeMeshId, cubeTransform);

        // Test 2: Green sphere
        Mesh sphere = MeshLoader::createSphere(1.5f, 16);
        uint32_t sphereMeshId = meshRenderer.createMesh(sphere);

        PBRMaterial greenMaterial = PBRMaterial::createDefault();
        greenMaterial.baseColor = glm::vec4(0.2f, 1.0f, 0.2f, 1.0f);
        greenMaterial.metallic = 0.0f;
        greenMaterial.roughness = 0.4f;
        uint32_t greenMatId = meshRenderer.createMaterial(greenMaterial);
        meshRenderer.setMeshMaterial(sphereMeshId, greenMatId);

        glm::mat4 sphereTransform = glm::translate(glm::mat4(1.0f),
                                                   glm::vec3(spawnX + 15.0f, spawnY + 3.0f, spawnZ));
        meshRenderer.createInstance(sphereMeshId, sphereTransform);

        // Test 3: Blue cylinder
        Mesh cylinder = MeshLoader::createCylinder(1.0f, 3.0f, 12);
        uint32_t cylinderMeshId = meshRenderer.createMesh(cylinder);

        PBRMaterial blueMaterial = PBRMaterial::createDefault();
        blueMaterial.baseColor = glm::vec4(0.2f, 0.4f, 1.0f, 1.0f);
        blueMaterial.metallic = 0.2f;
        blueMaterial.roughness = 0.3f;
        uint32_t blueMatId = meshRenderer.createMaterial(blueMaterial);
        meshRenderer.setMeshMaterial(cylinderMeshId, blueMatId);

        glm::mat4 cylinderTransform = glm::translate(glm::mat4(1.0f),
                                                     glm::vec3(spawnX + 5.0f, spawnY, spawnZ + 5.0f));
        meshRenderer.createInstance(cylinderMeshId, cylinderTransform);

        std::cout << "Mesh system ready: " << meshRenderer.getMeshCount() << " meshes, "
                 << meshRenderer.getInstanceCount() << " instances" << '\n';

        // ========== PLAYER MODEL SETUP ==========
        // Load player model for third-person view
        // Model should be placed in assets/models/player.glb
        uint32_t playerMeshId = 0;
        uint32_t playerInstanceId = 0;
        bool playerModelLoaded = false;
        float playerModelScale = 1.0f;

        try {
            std::string playerModelPath = "assets/models/player.glb";
            std::vector<PBRMaterial> playerMaterials;
            std::vector<Mesh> playerMeshes = MeshLoader::loadGLTF(playerModelPath, playerMaterials);

            if (!playerMeshes.empty()) {
                // Create mesh in renderer
                playerMeshId = meshRenderer.createMesh(playerMeshes[0]);

                // Apply material if loaded
                if (!playerMaterials.empty()) {
                    uint32_t playerMatId = meshRenderer.createMaterial(playerMaterials[0]);
                    meshRenderer.setMeshMaterial(playerMeshId, playerMatId);
                }

                // Calculate scale to make model 2 blocks tall
                // Get model bounds to determine its height
                glm::vec3 modelMin = playerMeshes[0].boundsMin;
                glm::vec3 modelMax = playerMeshes[0].boundsMax;
                float modelHeight = modelMax.y - modelMin.y;
                const float TARGET_HEIGHT = 2.0f; // 2 blocks tall
                playerModelScale = TARGET_HEIGHT / modelHeight;

                std::cout << "Player model loaded: original height=" << modelHeight
                         << ", scale=" << playerModelScale << " for 2 block height" << '\n';

                // Create instance (hidden by default in first-person)
                glm::mat4 playerTransform = glm::mat4(1.0f);
                playerInstanceId = meshRenderer.createInstance(playerMeshId, playerTransform);
                meshRenderer.setInstanceVisible(playerInstanceId, false); // Hidden in 1st person

                playerModelLoaded = true;
                std::cout << "Player model ready for third-person view (F3 to toggle)" << '\n';
            }
        } catch (const std::exception& e) {
            Logger::warning() << "Could not load player model: " << e.what();
            Logger::info() << "Place player.glb in assets/models/ for third-person view";
        }

        bool isPaused = false;
        bool escPressed = false;
        bool requestMouseReset = false;
        bool f9Pressed = false;
        bool iPressed = false;

        // Autosave timer (saves modified chunks every 5 minutes)
        float autosaveTimer = 0.0f;
        constexpr float AUTOSAVE_INTERVAL = 300.0f;  // 5 minutes in seconds

        // Viewport-based dynamic lighting system
        SunTracker sunTracker;

        std::cout << "Entering main loop..." << '\n';

        while (!glfwWindowShouldClose(window) && gameState == GameState::IN_GAME) {
            auto frameStart = std::chrono::high_resolution_clock::now();
            auto checkpoint = frameStart;

            float currentFrame = static_cast<float>(glfwGetTime());
            deltaTime = currentFrame - lastFrame;
            lastFrame = currentFrame;

            // Update FPS counter with UNCLAMPED deltaTime (before clamp!)
            DebugState::instance().updateFPS(deltaTime);

            // Clamp deltaTime to prevent physics explosions during lag spikes
            // Max 0.1 seconds (10 FPS minimum) prevents huge jumps when loading chunks
            float clampedDeltaTime = deltaTime;
            if (clampedDeltaTime > 0.1f) {
                clampedDeltaTime = 0.1f;
            }

            // PERFORMANCE MONITORING (2025-11-24): Begin frame tracking
            PerformanceMonitor::instance().beginFrame();

            // Autosave system (RAM cache → disk every 5 min)
            autosaveTimer += clampedDeltaTime;
            if (autosaveTimer >= AUTOSAVE_INTERVAL) {
                autosaveTimer = 0.0f;
                int savedChunks = world.saveModifiedChunks();
                if (savedChunks > 0) {
                    std::cout << "Autosave: saved " << savedChunks << " modified chunks" << '\n';
                }
            }

            glfwPollEvents();
            auto afterInput = std::chrono::high_resolution_clock::now();

            // Record input timing
            {
                auto inputDuration = std::chrono::duration_cast<std::chrono::microseconds>(afterInput - checkpoint);
                PerformanceMonitor::instance().recordTiming("input", inputDuration.count() / 1000.0f);
                checkpoint = afterInput;
            }

            // Update sky time (handles day/night cycle)
            ConsoleCommands::updateSkyTime(clampedDeltaTime);

            // Sun tracker removed - shader handles time-of-day by multiplying
            // static sky light values by dynamic sun/moon intensity
            // No need to recalculate voxel lighting when sun moves!

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
            player.update(window, clampedDeltaTime, &world, canProcessInput);

            // Update player model for third-person view
            if (playerModelLoaded) {
                // Toggle visibility based on view mode
                meshRenderer.setInstanceVisible(playerInstanceId, player.isThirdPerson());

                if (player.isThirdPerson()) {
                    // Update player model transform
                    glm::vec3 bodyPos = player.getBodyPosition();
                    glm::mat4 playerTransform = glm::translate(glm::mat4(1.0f), bodyPos);
                    // Rotate model to face same direction as player
                    playerTransform = glm::rotate(playerTransform, glm::radians(-player.Yaw - 90.0f), glm::vec3(0, 1, 0));
                    // Apply scale to fit 2 blocks tall
                    playerTransform = glm::scale(playerTransform, glm::vec3(playerModelScale));
                    meshRenderer.updateInstanceTransform(playerInstanceId, playerTransform);
                }
            }

            // Update inventory system
            inventory.update(window, clampedDeltaTime);

            // Update lighting system (OPTIMIZED: reduced from 60 FPS to 30 FPS for performance)
            // Still feels responsive but cuts CPU usage in half
            static float lightingUpdateTimer = 0.0f;
            lightingUpdateTimer += clampedDeltaTime;
            const float lightingUpdateInterval = 1.0f / 30.0f;  // 30 updates per second
            if (lightingUpdateTimer >= lightingUpdateInterval) {
                lightingUpdateTimer = 0.0f;
                if (DebugState::instance().lightingEnabled.getValue()) {
                    world.getLightingSystem()->update(clampedDeltaTime, &renderer);

                    // PERFORMANCE OPTIMIZATION (2025-11-23): Disabled interpolated lighting
                    // Was updating 32,768 values per chunk every frame = 40-80M operations/sec!
                    // Now uses direct lighting values for massive CPU savings on lower-end hardware
                    // world.updateInterpolatedLighting(clampedDeltaTime);
                }
            }

            // DECORATION FIX: Process pending decorations (chunks waiting for neighbors)
            // PERFORMANCE FIX (2025-11-24): Parallel decoration with 4-thread limit
            // Based on voxel engine best practices: smaller batches + higher frequency
            // With parallel processing (4 concurrent), can handle smaller batches efficiently
            // 10 chunks × 60Hz = 600 chunks/sec throughput (but 4 parallel = faster wall time)
            {
                auto decorationStart = std::chrono::high_resolution_clock::now();

                world.processPendingDecorations(&renderer, &worldStreaming, 10);  // Async mesh via worker threads

                auto decorationEnd = std::chrono::high_resolution_clock::now();
                auto decorationDuration = std::chrono::duration_cast<std::chrono::microseconds>(decorationEnd - decorationStart);
                PerformanceMonitor::instance().recordTiming("decoration", decorationDuration.count() / 1000.0f);

                // Record decoration queue sizes
                PerformanceMonitor::instance().recordQueueSize("pending_decorations", world.getPendingDecorationCount());
                PerformanceMonitor::instance().recordQueueSize("decorations_in_progress", world.getDecorationsInProgressCount());
            }

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

            // Update world streaming (load/unload chunks based on player position)
            // PERFORMANCE FIX: Only run streaming updates 4 times per second instead of 60 FPS
            // Reduces 374,640 iterations/second to 24,976 (99.3% reduction!)
            static float streamingUpdateTimer = 0.0f;
            streamingUpdateTimer += clampedDeltaTime;
            const float STREAMING_UPDATE_INTERVAL = 0.25f;  // 4 times per second
            const float renderDistance = 80.0f;  // Reduced from 120 for better performance

            if (streamingUpdateTimer >= STREAMING_UPDATE_INTERVAL) {
                streamingUpdateTimer = 0.0f;
                const float loadDistance = renderDistance + 32.0f;     // Load chunks slightly beyond render distance
                const float unloadDistance = renderDistance + 192.0f;  // Hysteresis: chunks stay loaded longer when moving away
                worldStreaming.updatePlayerPosition(player.Position, loadDistance, unloadDistance);
            }
            auto afterStreaming = std::chrono::high_resolution_clock::now();

            // Record streaming timing and queue sizes
            {
                auto streamingDuration = std::chrono::duration_cast<std::chrono::microseconds>(afterStreaming - checkpoint);
                PerformanceMonitor::instance().recordTiming("streaming", streamingDuration.count() / 1000.0f);

                auto stats = worldStreaming.getStats();
                PerformanceMonitor::instance().recordQueueSize("pending_loads", std::get<0>(stats));
                PerformanceMonitor::instance().recordQueueSize("completed_chunks", std::get<1>(stats));
                PerformanceMonitor::instance().recordQueueSize("mesh_queue", worldStreaming.getMeshQueueSize());

                checkpoint = afterStreaming;
            }

            // OPTIMIZATION: Process multiple chunks per frame with indirect drawing
            // ASYNC MESH GENERATION (2025-11-23): Main thread NEVER blocks!
            // Architecture:
            //   - Chunks added to world, mesh threads spawn DETACHED
            //   - Main thread returns immediately (no thread.join!)
            //   - GPU uploads happen for chunks ready from previous frames
            // History:
            //   v1-v3: BLOCKING (thread.join) → 100-200ms stalls
            //   v4: ASYNC (detached threads) → ZERO main thread blocking!
            // Can now process more chunks per frame since we never wait
#if USE_INDIRECT_DRAWING
            // PERFORMANCE FIX (2025-11-26): Increased from 1 to 8 chunks per frame
            // Phase 1 (add to world + queue mesh) is CPU-only and fast
            // Phase 2 (GPU upload) has its own rate limiting via getRecommendedUploadCount()
            // This 8x increase dramatically reduces "invisible chunk" lag
            worldStreaming.processCompletedChunks(8, 5.0f);  // Max 8 chunks, 5ms budget
#else
            worldStreaming.processCompletedChunks(4, 6.0f);   // Conservative for legacy path
#endif
            auto afterChunkProcess = std::chrono::high_resolution_clock::now();

            // Record chunk processing timing
            {
                auto chunkDuration = std::chrono::duration_cast<std::chrono::microseconds>(afterChunkProcess - checkpoint);
                PerformanceMonitor::instance().recordTiming("chunk_process", chunkDuration.count() / 1000.0f);
                checkpoint = afterChunkProcess;
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
            // PERFORMANCE: Reduced from 10x/second to 5x/second (still smooth enough)
            static float liquidUpdateTimer = 0.0f;
            liquidUpdateTimer += clampedDeltaTime;
            const float liquidUpdateInterval = 0.2f;  // Update liquids 5 times per second (was 10x)
            if (liquidUpdateTimer >= liquidUpdateInterval) {
                liquidUpdateTimer = 0.0f;
                world.updateWaterSimulation(clampedDeltaTime, &renderer, player.Position, renderDistance);
            }

            // Begin rendering
            if (!renderer.beginFrame()) {
                // Skip this frame (swap chain recreation in progress)
                continue;
            }
            auto afterBeginFrame = std::chrono::high_resolution_clock::now();

            // Get current descriptor set (need to store it to take address)
            VkDescriptorSet currentDescriptorSet = renderer.getCurrentDescriptorSet();

            // Reset pipeline cache at frame start (GPU optimization)
            renderer.resetPipelineCache();

            // Render skybox first (renders behind everything)
            renderer.renderSkybox();

            // Render world with normal or wireframe pipeline based on debug mode
            VkPipeline worldPipeline = DebugState::instance().wireframeMode.getValue() ?
                renderer.getWireframePipeline() : renderer.getGraphicsPipeline();
            renderer.bindPipelineCached(renderer.getCurrentCommandBuffer(), worldPipeline);
            vkCmdBindDescriptorSets(renderer.getCurrentCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                                   renderer.getPipelineLayout(), 0, 1, &currentDescriptorSet, 0, nullptr);
            world.renderWorld(renderer.getCurrentCommandBuffer(), player.Position, viewProj, renderDistance, &renderer);
            auto afterWorldRender = std::chrono::high_resolution_clock::now();

            // Render meshes (shares depth buffer with voxels)
            meshRenderer.render(renderer.getCurrentCommandBuffer());

            // Render block outline with line pipeline
            if (target.hasTarget) {
                renderer.bindPipelineCached(renderer.getCurrentCommandBuffer(), renderer.getLinePipeline());
                vkCmdBindDescriptorSets(renderer.getCurrentCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                                       renderer.getPipelineLayout(), 0, 1, &currentDescriptorSet, 0, nullptr);
                targetingSystem.renderBlockOutline(renderer.getCurrentCommandBuffer());
            }

            // Render ImGui (crosshair when playing, menu when paused, console overlay)
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            auto afterImGuiStart = std::chrono::high_resolution_clock::now();

            if (isPaused) {
                PauseMenuAction pauseAction = pauseMenu.render();
                if (pauseAction == PauseMenuAction::RESUME) {
                    isPaused = false;
                    requestMouseReset = true;
                } else if (pauseAction == PauseMenuAction::EXIT_TO_MENU) {
                    // Save and exit to main menu
                    std::cout << "Exiting to main menu..." << '\n';

                    // Save world, player, and inventory
                    std::string exitSaveWorldPath = "worlds/world_" + std::to_string(seed);
                    std::cout << "Saving world to " << exitSaveWorldPath << "..." << '\n';
                    if (world.saveWorld(exitSaveWorldPath)) {
                        std::cout << "World saved successfully" << '\n';
                    }
                    if (player.savePlayerState(exitSaveWorldPath)) {
                        std::cout << "Player state saved successfully" << '\n';
                    }
                    if (inventory.save(exitSaveWorldPath)) {
                        std::cout << "Inventory saved successfully" << '\n';
                    }

                    // Return to main menu by breaking game loop
                    gameState = GameState::MAIN_MENU;
                    isPaused = false;
                } else if (pauseAction == PauseMenuAction::QUIT) {
                    // Save before quitting
                    std::cout << "Quitting to desktop..." << '\n';
                    std::string quitSaveWorldPath = "worlds/world_" + std::to_string(seed);
                    std::cout << "Saving world to " << quitSaveWorldPath << "..." << '\n';
                    if (world.saveWorld(quitSaveWorldPath)) {
                        std::cout << "World saved successfully" << '\n';
                    }
                    if (player.savePlayerState(quitSaveWorldPath)) {
                        std::cout << "Player state saved successfully" << '\n';
                    }
                    if (inventory.save(quitSaveWorldPath)) {
                        std::cout << "Inventory saved successfully" << '\n';
                    }
                    glfwSetWindowShouldClose(window, GLFW_TRUE);
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
            auto renderEnd = std::chrono::high_resolution_clock::now();

            // Record render timing
            {
                auto renderDuration = std::chrono::duration_cast<std::chrono::microseconds>(renderEnd - checkpoint);
                PerformanceMonitor::instance().recordTiming("render", renderDuration.count() / 1000.0f);
            }

            // Record player position and distance from spawn
            static glm::vec3 spawnPos(spawnX, spawnY, spawnZ);
            PerformanceMonitor::instance().recordPlayerPosition(player.Position, spawnPos);

            // End performance monitoring frame
            PerformanceMonitor::instance().endFrame();

            renderer.endFrame();
            auto frameEnd = std::chrono::high_resolution_clock::now();

            // PERFORMANCE DIAGNOSTICS: Log slow frames (> 50ms = < 20 FPS)
            auto frameDuration = std::chrono::duration_cast<std::chrono::milliseconds>(frameEnd - frameStart).count();
            if (frameDuration > 50) {
                auto inputMs = std::chrono::duration_cast<std::chrono::milliseconds>(afterInput - frameStart).count();
                auto streamMs = std::chrono::duration_cast<std::chrono::milliseconds>(afterStreaming - afterInput).count();
                auto chunkProcMs = std::chrono::duration_cast<std::chrono::milliseconds>(afterChunkProcess - afterStreaming).count();
                auto beginFrameMs = std::chrono::duration_cast<std::chrono::milliseconds>(afterBeginFrame - afterChunkProcess).count();
                auto worldRenderMs = std::chrono::duration_cast<std::chrono::milliseconds>(afterWorldRender - afterBeginFrame).count();
                auto imguiStartMs = std::chrono::duration_cast<std::chrono::milliseconds>(afterImGuiStart - afterWorldRender).count();
                auto imguiEndMs = std::chrono::duration_cast<std::chrono::milliseconds>(renderEnd - afterImGuiStart).count();
                auto presentMs = std::chrono::duration_cast<std::chrono::milliseconds>(frameEnd - renderEnd).count();

                std::cerr << "[PERF] SLOW FRAME " << frameDuration << "ms: "
                         << "input=" << inputMs << " | "
                         << "stream=" << streamMs << " | "
                         << "chunkProc=" << chunkProcMs << " | "
                         << "beginFrame=" << beginFrameMs << " | "
                         << "worldRender=" << worldRenderMs << " | "
                         << "imguiStart=" << imguiStartMs << " | "
                         << "imguiEnd=" << imguiEndMs << " | "
                         << "present=" << presentMs << "ms" << '\n';
            }
        }

                // Game loop ended - check why it ended

                // If window was closed (user clicked X), save first
                if (glfwWindowShouldClose(window) && gameState == GameState::IN_GAME) {
                    std::cout << "Window closed during gameplay - saving..." << '\n';
                    std::string closeSaveWorldPath = "worlds/world_" + std::to_string(seed);
                    if (world.saveWorld(closeSaveWorldPath)) {
                        std::cout << "World saved successfully" << '\n';
                    }
                    if (player.savePlayerState(closeSaveWorldPath)) {
                        std::cout << "Player state saved successfully" << '\n';
                    }
                    if (inventory.save(closeSaveWorldPath)) {
                        std::cout << "Inventory saved successfully" << '\n';
                    }
                }

                // Check if we're returning to menu or shutting down
                if (gameState == GameState::MAIN_MENU) {
                    // Return to main menu - cleanup game resources but keep ImGui/Vulkan alive
                    std::cout << "Returning to main menu..." << '\n';

                    // Stop world streaming
                    std::cout << "  Stopping world streaming..." << '\n';
                    worldStreaming.stop();

                    // Wait for GPU to finish before cleanup
                    std::cout << "  Waiting for GPU to finish..." << '\n';
                    vkDeviceWaitIdle(renderer.getDevice());

                    // Cleanup world chunk buffers
                    std::cout << "  Cleaning up world resources..." << '\n';
                    world.cleanup(&renderer);

                    // Note: Save was already done when EXIT_TO_MENU was pressed
                    std::cout << "Ready to show main menu" << '\n';

                    // Continue outer loop - will show main menu again
                    continue;
                }
            }  // End of if (gameState == IN_GAME)

        // If we reach here, loop continues (back to menu check)
        }  // End of outer game loop

    // ========== FULL SHUTDOWN ==========
    // Only reached when player quits
    // Note: Game is already saved if it was running (via QUIT or EXIT_TO_MENU handlers)
    std::cout << "Shutting down..." << '\n';

        // Stop event dispatcher (processes remaining events before stopping)
        std::cout << "  Stopping event dispatcher..." << '\n';
        EventDispatcher::instance().stop();

        // Wait for device to finish before cleanup
        std::cout << "  Waiting for GPU to finish..." << '\n';
        vkDeviceWaitIdle(renderer.getDevice());
        std::cout << "  GPU idle" << '\n';

        // Cleanup ImGui
        std::cout << "  Cleaning up ImGui..." << '\n';
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        vkDestroyDescriptorPool(renderer.getDevice(), imguiPool, nullptr);
        std::cout << "  ImGui cleanup complete" << '\n';

        // Save current window size to config
        std::cout << "  Saving config..." << '\n';
        int currentWidth, currentHeight;
        glfwGetWindowSize(window, &currentWidth, &currentHeight);
        config.setInt("Window", "width", currentWidth);
        config.setInt("Window", "height", currentHeight);
        config.saveToFile("config.ini");
        std::cout << "  Config saved" << '\n';

        std::cout << "  Destroying window..." << '\n';
        glfwDestroyWindow(window);
        std::cout << "  Terminating GLFW..." << '\n';
        glfwTerminate();
        std::cout << "  Cleaning up noise..." << '\n';
        Chunk::cleanupNoise();

        std::cout << "Shutdown complete." << '\n';
        std::cout << "Exiting main()..." << '\n';
        std::cout.flush();  // Force flush before destructors run
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << '\n';
        return -1;
    }

    std::cout << "After exception handling" << '\n';
    std::cout.flush();
}
