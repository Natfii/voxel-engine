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
#include "crosshair.h"
#include "config.h"
#include "raycast.h"
#include "block_outline.h"
#include "console.h"
#include "console_commands.h"
#include "debug_state.h"

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
        // Convert terrain height (in blocks) to world units, add 3 blocks clearance, then add eye height
        // Player position is at eye level, not feet level
        float spawnY = (terrainHeight + 3) * 0.5f + 0.8f;  // terrain + 3 blocks + eye height from feet

        std::cout << "Spawning player at world center (" << spawnX << ", " << spawnY << ", " << spawnZ << ")" << std::endl;
        Player player(glm::vec3(spawnX, spawnY, spawnZ), glm::vec3(0.0f, 1.0f, 0.0f), -90.0f, 0.0f);

        PauseMenu pauseMenu(window);
        Crosshair crosshair;
        BlockOutline blockOutline;
        blockOutline.init(&renderer);

        // Create console and register commands
        Console console(window);
        ConsoleCommands::registerAll(&console, &player, &world);

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

            // Handle F9 key for console
            if (glfwGetKey(window, GLFW_KEY_F9) == GLFW_PRESS) {
                if (!f9Pressed) {
                    f9Pressed = true;
                    console.toggle();

                    // Enable/disable cursor based on console visibility
                    if (console.isVisible()) {
                        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
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

            // Sync player noclip with debug state
            player.NoclipMode = DebugState::instance().noclip.getValue();

            // Update player if not paused and console is closed
            if (!isPaused && !console.isVisible()) {
                player.update(window, deltaTime, &world);
            }

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

            // Update uniform buffer with camera position and render distance for fog
            const float renderDistance = 80.0f;
            renderer.updateUniformBuffer(renderer.getCurrentFrame(), model, view, projection, player.Position, renderDistance);

            // Raycast to find targeted block (5 blocks = 2.5 world units since blocks are 0.5 units)
            RaycastHit hit = Raycast::castRay(&world, player.Position, player.Front, 2.5f);

            // Handle block breaking on left mouse click
            static bool leftMousePressed = false;
            if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
                if (!leftMousePressed && hit.hit) {
                    leftMousePressed = true;
                    // Break the block (set to air = 0)
                    world.setBlockAt(hit.position.x, hit.position.y, hit.position.z, 0);
                    // TODO: Add block break animation here later

                    // Update the affected chunk and all adjacent chunks
                    // Must regenerate MESH (not just vertex buffer) because face culling needs updating
                    Chunk* affectedChunk = world.getChunkAtWorldPos(hit.position.x, hit.position.y, hit.position.z);
                    if (affectedChunk) {
                        affectedChunk->generateMesh(&world);
                        affectedChunk->createVertexBuffer(&renderer);
                    }

                    // Always update all 6 adjacent chunks (not just on boundaries)
                    // This handles cases like breaking grass revealing stone below
                    Chunk* neighbors[6] = {
                        world.getChunkAtWorldPos(hit.position.x - 0.5f, hit.position.y, hit.position.z),  // -X
                        world.getChunkAtWorldPos(hit.position.x + 0.5f, hit.position.y, hit.position.z),  // +X
                        world.getChunkAtWorldPos(hit.position.x, hit.position.y - 0.5f, hit.position.z),  // -Y (below)
                        world.getChunkAtWorldPos(hit.position.x, hit.position.y + 0.5f, hit.position.z),  // +Y (above)
                        world.getChunkAtWorldPos(hit.position.x, hit.position.y, hit.position.z - 0.5f),  // -Z
                        world.getChunkAtWorldPos(hit.position.x, hit.position.y, hit.position.z + 0.5f)   // +Z
                    };

                    // Regenerate mesh and buffer for each unique neighbor chunk
                    for (int i = 0; i < 6; i++) {
                        if (neighbors[i] && neighbors[i] != affectedChunk) {
                            // Skip if already updated (same chunk)
                            bool alreadyUpdated = false;
                            for (int j = 0; j < i; j++) {
                                if (neighbors[j] == neighbors[i]) {
                                    alreadyUpdated = true;
                                    break;
                                }
                            }
                            if (!alreadyUpdated) {
                                neighbors[i]->generateMesh(&world);
                                neighbors[i]->createVertexBuffer(&renderer);
                            }
                        }
                    }
                }
            } else {
                leftMousePressed = false;
            }

            // Update block outline if we're looking at a block
            if (hit.hit) {
                blockOutline.setPosition(hit.position.x, hit.position.y, hit.position.z);
                blockOutline.updateBuffer(&renderer);
            } else {
                blockOutline.setVisible(false);
            }

            // Begin rendering
            if (!renderer.beginFrame()) {
                // Skip this frame (swap chain recreation in progress)
                continue;
            }

            // Get current descriptor set (need to store it to take address)
            VkDescriptorSet currentDescriptorSet = renderer.getCurrentDescriptorSet();

            // Render world with normal pipeline
            vkCmdBindPipeline(renderer.getCurrentCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, renderer.getGraphicsPipeline());
            vkCmdBindDescriptorSets(renderer.getCurrentCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                                   renderer.getPipelineLayout(), 0, 1, &currentDescriptorSet, 0, nullptr);
            world.renderWorld(renderer.getCurrentCommandBuffer(), player.Position, 80.0f);

            // Render block outline with line pipeline
            if (hit.hit) {
                vkCmdBindPipeline(renderer.getCurrentCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, renderer.getLinePipeline());
                vkCmdBindDescriptorSets(renderer.getCurrentCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                                       renderer.getPipelineLayout(), 0, 1, &currentDescriptorSet, 0, nullptr);
                blockOutline.render(renderer.getCurrentCommandBuffer());
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
                crosshair.render();
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

            ImGui::Render();
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), renderer.getCurrentCommandBuffer());

            // End rendering
            renderer.endFrame();
        }

        // Wait for device to finish before cleanup
        vkDeviceWaitIdle(renderer.getDevice());

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
