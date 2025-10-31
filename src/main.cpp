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

        std::cout << "Loading block registry..." << std::endl;
        BlockRegistry::instance().loadBlocks("assets/blocks");

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
        float spawnY = (terrainHeight + 2) * 0.5f;  // +2 blocks above terrain, scaled to 0.5 units

        std::cout << "Spawning player at world center (" << spawnX << ", " << spawnY << ", " << spawnZ << ")" << std::endl;
        Player player(glm::vec3(spawnX, spawnY, spawnZ), glm::vec3(0.0f, 1.0f, 0.0f), -90.0f, 0.0f);

        PauseMenu pauseMenu(window);
        Crosshair crosshair;
        BlockOutline blockOutline;
        blockOutline.init(&renderer);
        bool isPaused = false;
        bool escPressed = false;
        bool requestMouseReset = false;

        std::cout << "Entering main loop..." << std::endl;

        while (!glfwWindowShouldClose(window)) {
            float currentFrame = glfwGetTime();
            deltaTime = currentFrame - lastFrame;
            lastFrame = currentFrame;

            glfwPollEvents();

            // Handle ESC key for pause menu
            if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
                if (!escPressed) {
                    escPressed = true;
                    isPaused = !isPaused;
                    if (isPaused) {
                        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                    } else {
                        requestMouseReset = true;
                    }
                }
            } else {
                escPressed = false;
            }

            // Update player if not paused
            if (!isPaused) {
                player.update(window, deltaTime);
            }

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
            glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 300.0f);
            // Flip Y axis for Vulkan (Vulkan's Y axis points down in NDC, OpenGL's points up)
            projection[1][1] *= -1;

            // Update uniform buffer
            renderer.updateUniformBuffer(renderer.getCurrentFrame(), model, view, projection);

            // Raycast to find targeted block (5 blocks = 2.5 world units since blocks are 0.5 units)
            RaycastHit hit = Raycast::castRay(&world, player.Position, player.Front, 2.5f);

            // Update block outline if we're looking at a block
            if (hit.hit) {
                blockOutline.setPosition(hit.position.x, hit.position.y, hit.position.z);
                blockOutline.updateBuffer(&renderer);
            } else {
                blockOutline.setVisible(false);
            }

            // Begin rendering
            renderer.beginFrame();

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

            // Render ImGui (crosshair when playing, menu when paused)
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            if (isPaused) {
                if (pauseMenu.render()) {
                    isPaused = false;
                    requestMouseReset = true;
                }
            } else {
                crosshair.render();
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
