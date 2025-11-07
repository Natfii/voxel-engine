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
#include "player.h"
#include "pause_menu.h"
#include "config.h"
#include "console.h"
#include "console_commands.h"
#include "debug_state.h"
#include "targeting_system.h"
#include "input_manager.h"

// Global variables
VulkanRenderer* g_renderer = nullptr;

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    if (g_renderer) {
        g_renderer->framebufferResized();
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

        // Bind texture atlas to renderer descriptor sets
        std::cout << "Binding texture atlas..." << std::endl;
        renderer.bindAtlasTexture(
            BlockRegistry::instance().getAtlasImageView(),
            BlockRegistry::instance().getAtlasSampler()
        );

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

        bool isPaused = false;
        bool escPressed = false;
        bool requestMouseReset = false;
        bool f9Pressed = false;

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
                    } else if (!isPaused) {
                        requestMouseReset = true;
                    }
                }
            } else {
                f9Pressed = false;
            }

            // Handle ESC key for pause menu (but not if console is open)
            if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
                if (!escPressed) {
                    escPressed = true;

                    // If console is open, close it instead of opening pause menu
                    if (console.isVisible()) {
                        console.setVisible(false);
                        if (!isPaused) {
                            requestMouseReset = true;
                        }
                    } else {
                        isPaused = !isPaused;
                        if (isPaused) {
                            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                        } else {
                            requestMouseReset = true;
                        }
                    }
                }
            } else {
                escPressed = false;
            }

            // Update input context based on menu/console state
            if (isPaused) {
                InputManager::instance().setContext(InputManager::Context::PAUSED);
            } else if (console.isVisible()) {
                InputManager::instance().setContext(InputManager::Context::CONSOLE);
            } else {
                InputManager::instance().setContext(InputManager::Context::GAMEPLAY);
            }

            // Always update player physics, but only process input during gameplay
            bool canProcessInput = InputManager::instance().canMove();
            player.update(window, deltaTime, &world, canProcessInput);

            // Check if console was closed (by clicking outside or ESC)
            // and we need to reset mouse for gameplay
            static bool wasConsoleOpen = false;
            if (wasConsoleOpen && !console.isVisible() && !isPaused) {
                requestMouseReset = true;
            }
            wasConsoleOpen = console.isVisible();

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
            } else if (!console.isVisible()) {
                targetingSystem.renderCrosshair();
            }

            // Render console (overlays on top of everything)
            console.render();

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
