#include <iostream>
#include <cmath>
// OpenGL/GLFW/GLEW headers
#include <GL/glew.h>
#include <GLFW/glfw3.h>
// GLM for matrix transformations
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "chunk.h"  // Chunk class

// Callback for window resize: adjust viewport
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

// Callback for mouse movement: update camera yaw/pitch
double lastX = 400, lastY = 300;
bool firstMouse = true;
float yaw = -90.0f;   // yaw initialized to -90 deg so initial direction is -Z
float pitch = 0.0f;
void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (firstMouse) {
        // On first call, set the last coordinates to the current ones
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    // Calculate mouse offset since last frame
    float xoffset = static_cast<float>(xpos - lastX);
    float yoffset = static_cast<float>(lastY - ypos);  // reversed: y ranges bottom->top
    lastX = xpos;
    lastY = ypos;

    // Apply sensitivity factor
    const float sensitivity = 0.1f;
    xoffset *= sensitivity;
    yoffset *= sensitivity;

    // Update yaw and pitch
    yaw   += xoffset;
    pitch += yoffset;

    // Constrain the pitch angle to avoid flipping (looking too far up/down)
    if (pitch > 89.0f)  pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;
}

// Camera settings
glm::vec3 cameraPos   = glm::vec3(0.0f, 2.0f, 5.0f);  // Starting position
glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f); // Initial direction
glm::vec3 cameraUp    = glm::vec3(0.0f, 1.0f,  0.0f); // World up vector

float deltaTime = 0.0f; // Time between current frame and last frame
float lastFrame = 0.0f;

void processInput(GLFWwindow* window) {
    // Close window on ESC
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    }
    // Movement speed
    float cameraSpeed = 5.0f * deltaTime;
    // WASD or arrow keys for movement
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) {
        // Move forward in the direction of cameraFront
        cameraPos += cameraSpeed * cameraFront;
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) {
        // Move backward
        cameraPos -= cameraSpeed * cameraFront;
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) {
        // Move left
        cameraPos -= glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
        // Move right
        cameraPos += glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;
    }
}

int main() {
    // Initialize GLFW and create a window
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
    // Register callbacks
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    // Capture the mouse
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // Initialize GLEW (load OpenGL function pointers)
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW\n";
        glfwTerminate();
        return -1;
    }
    glEnable(GL_DEPTH_TEST);

    // Build and compile shaders
    const char* vertexShaderSource = R"(
        #version 330 core
        layout(location = 0) in vec3 aPos;    // Vertex position
        layout(location = 1) in vec3 aColor;  // Vertex color
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

    // Utility to compile a shader and check for errors
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
    // Link shaders into a program
    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertShader);
    glAttachShader(shaderProgram, fragShader);
    glLinkProgram(shaderProgram);
    // Check linking status
    GLint linked;
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        char infoLog[512];
        glGetProgramInfoLog(shaderProgram, 512, nullptr, infoLog);
        std::cerr << "ERROR: Shader linking failed\n" << infoLog << std::endl;
    }
    glDeleteShader(vertShader);
    glDeleteShader(fragShader);

    // Define colors for block types (unused here; coloring done in chunk.generate())
    glm::vec3 colorStone(0.6f, 0.6f, 0.6f);
    glm::vec3 colorGrass(0.3f, 0.8f, 0.3f);
    glm::vec3 colorDirt(0.59f, 0.29f, 0.0f);

    // Create and initialize a chunk at (0,0,0)
    Chunk chunk(0, 0, 0);
    // Fill chunk blocks and generate mesh geometry
    chunk.generate();
    // (Inside Chunk::generate, m_blocks is filled and the vertex buffer is created)
    // After generation, the mesh for all block faces is ready in chunk.m_vao.

    // Retrieve uniform locations for matrices in the shader
    glUseProgram(shaderProgram);
    GLint modelLoc      = glGetUniformLocation(shaderProgram, "uModel");
    GLint viewLoc       = glGetUniformLocation(shaderProgram, "uView");
    GLint projectionLoc = glGetUniformLocation(shaderProgram, "uProjection");

    // Main render loop
    while (!glfwWindowShouldClose(window)) {
        // Time logic
        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // Input handling
        processInput(window);

        // Camera orientation from mouse movement
        glm::vec3 direction;
        direction.x = cos(glm::radians(pitch)) * cos(glm::radians(yaw));
        direction.y = sin(glm::radians(pitch));
        direction.z = cos(glm::radians(pitch)) * sin(glm::radians(yaw));
        cameraFront = glm::normalize(direction);

        // Prepare transformation matrices
        glm::mat4 model = glm::mat4(1.0f);
        // If the chunk had a world offset (m_x, m_y, m_z), apply it here:
        // model = glm::translate(model, glm::vec3(chunkXOffset, chunkYOffset, chunkZOffset));
        glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        float aspect = float(width) / float(height);
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);

        // Render
        glClearColor(0.529f, 0.808f, 0.922f, 1.0f);  // Sky blue background
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(shaderProgram);
        // Pass matrices to the shader
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix4fv(viewLoc, 1, GL_FALSE,  glm::value_ptr(view));
        glUniformMatrix4fv(projectionLoc, 1, GL_FALSE, glm::value_ptr(projection));

        // Draw the chunk mesh using our shader
        chunk.render();

        // Swap buffers and poll events
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Cleanup and exit
    glfwTerminate();
    return 0;
}
