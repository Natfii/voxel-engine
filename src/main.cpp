#include <iostream>
#include <cmath>
// OpenGL/GLFW/GLEW headers
#include <GL/glew.h>
#include <GLFW/glfw3.h>
// GLM for matrix transformations
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
// ImGui headers
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
// Project headers
#include "chunk.h"
#include "world.h"
#include "block_system.h"
#include "player.h"
#include "pause_menu.h"

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

float deltaTime = 0.0f;
float lastFrame = 0.0f;

int main() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(800, 600, "Voxel Chunk", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW\n";
        glfwTerminate();
        return -1;
    }
    glEnable(GL_DEPTH_TEST);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    const char* vertexShaderSource = R"(
        #version 330 core
        layout(location = 0) in vec3 aPos;
        layout(location = 1) in vec3 aColor;
        out vec3 vColor;
        uniform mat4 uModel;
        uniform mat4 uView;
        uniform mat4 uProjection;
        void main() {
            vColor = aColor;
            gl_Position = uProjection * uView * uModel * vec4(aPos, 1.0);
        }
    )";

    const char* fragmentShaderSource = R"(
        #version 330 core
        in vec3 vColor;
        out vec4 FragColor;
        void main() {
            FragColor = vec4(vColor, 1.0);
        }
    )";

    auto compileShader = [&](GLenum type, const char* src) -> GLuint {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);
        GLint success;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (success != GL_TRUE) {
            char infoLog[512];
            glGetShaderInfoLog(shader, 512, nullptr, infoLog);
            std::cerr << "ERROR: Shader compilation failed\n" << infoLog << std::endl;
        }
        return shader;
    };

    GLuint vertShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint fragShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertShader);
    glAttachShader(shaderProgram, fragShader);
    glLinkProgram(shaderProgram);
    GLint linked;
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        char infoLog[512];
        glGetProgramInfoLog(shaderProgram, 512, nullptr, infoLog);
        std::cerr << "ERROR: Shader linking failed\n" << infoLog << std::endl;
    }
    glDeleteShader(vertShader);
    glDeleteShader(fragShader);

    BlockRegistry::instance().loadBlocks("assets/blocks");
    World world(2, 1, 2);
    world.generateWorld();

    Player player(glm::vec3(0.0f, 8.0f, 12.0f), glm::vec3(0.0f, 1.0f, 0.0f), -90.0f, 0.0f);
    PauseMenu pauseMenu(window);
    bool isPaused = false;
    bool escPressed = false;
    bool requestMouseReset = false;

    glUseProgram(shaderProgram);
    GLint modelLoc      = glGetUniformLocation(shaderProgram, "uModel");
    GLint viewLoc       = glGetUniformLocation(shaderProgram, "uView");
    GLint projectionLoc = glGetUniformLocation(shaderProgram, "uProjection");

    while (!glfwWindowShouldClose(window)) {
        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        glfwPollEvents();

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            if (!escPressed) {
                escPressed = true;
                isPaused = !isPaused;
                if (isPaused) {
                    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                }
            }
        } else {
            escPressed = false;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (!isPaused) {
            player.update(window, deltaTime);
        } else {
            if (pauseMenu.render()) {
                isPaused = false;
                requestMouseReset = true;
            }
        }

        if (requestMouseReset) {
            player.resetMouse();
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            requestMouseReset = false;
        }

        glm::mat4 model = glm::mat4(1.0f);
        glm::mat4 view = player.getViewMatrix();
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        float aspect = float(width) / float(height);
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);

        glClearColor(0.529f, 0.808f, 0.922f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(shaderProgram);
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(projectionLoc, 1, GL_FALSE, glm::value_ptr(projection));

        world.renderWorld();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwTerminate();
    return 0;
}