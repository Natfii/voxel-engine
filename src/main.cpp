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
#include "structure_system.h"
#include "player.h"
#include "pause_menu.h"
#include "config.h"
#include "console.h"
#include "console_commands.h"
#include "debug_state.h"
#include "targeting_system.h"
#include "raycast.h"
#include "input_manager.h"
#include "inventory.h"
// BlockIconRenderer is now part of block_system.h

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
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // No OpenGL context
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);     // Allow window resizing
        GLFWwindow* window = glfwCreateWindow(800, 600, "Voxel Engine - Vulkan", nullptr, nullptr);
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

        // Load configuration
        Config& config = Config::instance();
        if (!config.loadFromFile("config.ini")) {
            std::cerr << "Warning: Failed to load config.ini, using default values" << std::endl;
        }

        // Get world configuration from config file
        int seed = config.getInt("World", "seed", 1124345);
        int worldWidth = config.getInt("World", "world_width", 12);
        int worldHeight = config.getInt("World", "world_height", 3);
        int worldDepth = config.getInt("World", "world_depth", 12);

        std::cout << "Loading block registry with textures..." << std::endl;
        BlockRegistry::instance().loadBlocks("assets/blocks", &renderer);

        std::cout << "Loading structure registry..." << std::endl;
        StructureRegistry::instance().loadStructures("assets/structures");

        // Bind texture atlas to renderer descriptor sets
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

        std::cout << "Initializing world generation..." << std::endl;
        Chunk::initNoise(seed);
        World world(worldWidth, worldHeight, worldDepth);

        std::cout << "Generating world..." << std::endl;
        world.generateWorld();

        std::cout << "Creating GPU buffers..." << std::endl;
        world.createBuffers(&renderer);

        // Spawn player at world center with appropriate height based on terrain
        // World is centered around (0, 0), so spawn at center
        float spawnX = 0.0f;
        float spawnZ = 0.0f;
        int terrainHeight = Chunk::getTerrainHeightAt(spawnX, spawnZ);
        // Convert terrain height (in blocks) to world units, add 5 blocks clearance, then add eye height
        // Player position is at eye level, not feet level
        float spawnY = (terrainHeight + 5) * 0.5f + 0.8f;  // terrain + 5 blocks clearance + eye height

        // Ensure player never spawns below safe height (minimum y = 35.0 - well above terrain)
        if (spawnY < 35.0f) {
            std::cout << "Warning: Calculated spawn Y (" << spawnY << ") from terrain height " << terrainHeight << " is too low, setting to 35.0" << std::endl;
            spawnY = 35.0f;
        }

        std::cout << "Spawning player at (" << spawnX << ", " << spawnY << ", " << spawnZ << ") - terrain height: " << terrainHeight << " blocks" << std::endl;
        Player player(glm::vec3(spawnX, spawnY, spawnZ), glm::vec3(0.0f, 1.0f, 0.0f), -90.0f, 0.0f);

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

        bool isPaused = false;
        bool escPressed = false;
        bool requestMouseReset = false;
        bool f9Pressed = false;
        bool iPressed = false;

        std::cout << "Entering main loop..." << std::endl;

        while (!glfwWindowShouldClose(window)) {
            float currentFrame = glfwGetTime();
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
            const float renderDistance = 80.0f;
            renderer.updateUniformBuffer(renderer.getCurrentFrame(), model, view, projection, player.Position, renderDistance);

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
                        if (selectedItem.blockID > 0) {
                            glm::vec3 placePosition = target.blockPosition + target.hitNormal * 0.5f;
                            world.placeBlock(placePosition, selectedItem.blockID, &renderer);
                        }
                    } else if (selectedItem.type == InventoryItemType::STRUCTURE) {
                        // Place structure at ground level below targeted position
                        // Convert target block position to block coordinates
                        glm::ivec3 targetBlockCoords(
                            (int)(target.blockPosition.x / 0.5f),
                            (int)(target.blockPosition.y / 0.5f),
                            (int)(target.blockPosition.z / 0.5f)
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

            // Update liquid physics periodically
            // DISABLED: Scanning all blocks in the world is too expensive for large worlds (14M+ checks per update)
            // TODO: Optimize by maintaining a list of active liquid blocks instead of scanning entire world
            /*
            static float liquidUpdateTimer = 0.0f;
            liquidUpdateTimer += deltaTime;
            const float liquidUpdateInterval = 0.5f;  // Update liquids 2 times per second
            if (liquidUpdateTimer >= liquidUpdateInterval) {
                liquidUpdateTimer = 0.0f;
                world.updateLiquids(&renderer);
            }
            */

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
            world.renderWorld(renderer.getCurrentCommandBuffer(), player.Position, viewProj, 80.0f);

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

        // Wait for device to finish before cleanup
        vkDeviceWaitIdle(renderer.getDevice());

        // Cleanup world chunk buffers
        world.cleanup(&renderer);

        // Cleanup ImGui
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        vkDestroyDescriptorPool(renderer.getDevice(), imguiPool, nullptr);

        glfwDestroyWindow(window);
        glfwTerminate();
        Chunk::cleanupNoise();

        std::cout << "Shutdown complete." << std::endl;
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return -1;
    }
}
