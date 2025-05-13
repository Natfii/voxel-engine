#include <iostream>
#include <cmath>
// OpenGL/GLFW/GLEW headers
#include <GL/glew.h>
#include <GLFW/glfw3.h>
// GLM for matrix transformations
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "chunk.h"  // Chunk class with BlockType enum and methods

// Callback for window resize: adjust viewport
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

// Callback for mouse movement: update camera yaw/pitch
double lastX = 400, lastY = 300;
bool firstMouse = true;
float yaw = -90.0f;   // yaw initialized to -90 deg to look toward -Z
float pitch = 0.0f;
void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (firstMouse) {
        // on first mouse event, just initialize lastX/Y
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }
    // Calculate mouse offset from last frame
    float xoffset = static_cast<float>(xpos - lastX);
    float yoffset = static_cast<float>(lastY - ypos);  // reversed: y ranges top->bottom
    lastX = xpos;
    lastY = ypos;
    // Apply sensitivity scaling
    const float sensitivity = 0.1f;
    xoffset *= sensitivity;
    yoffset *= sensitivity;
    // Update yaw and pitch
    yaw   += xoffset;
    pitch += yoffset;
    // Constrain the pitch angle to avoid flipping:contentReference[oaicite:7]{index=7}
    if (pitch > 89.0f)  pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;
}

// Camera settings and global variables
glm::vec3 cameraPos   = glm::vec3(0.0f, 2.0f, 5.0f);  // starting camera position
glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f); // initial direction
glm::vec3 cameraUp    = glm::vec3(0.0f, 1.0f,  0.0f);  // world up vector
float deltaTime = 0.0f;
float lastFrame = 0.0f;

// Process keyboard input for camera movement and rotation
void processInput(GLFWwindow* window) {
    // Close window on ESC
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    }
    // Movement speed
    float cameraSpeed = 5.0f * deltaTime; // units per second
    // WASD or arrow keys for movement:contentReference[oaicite:8]{index=8}
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS || 
        glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) {
        // Move forward (in the direction cameraFront)
        cameraPos += cameraSpeed * cameraFront;
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS || 
        glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) {
        // Move backward
        cameraPos -= cameraSpeed * cameraFront;
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS || 
        glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) {
        // Strafe left (move perpendicular to cameraFront)
        cameraPos -= glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS || 
        glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
        // Strafe right
        cameraPos += glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;
    }
    // Optional keys for rotating view (if not using mouse):
    float rotSpeed = 90.0f * deltaTime; // rotation speed in degrees per second
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
        yaw -= rotSpeed;
    }
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
        yaw += rotSpeed;
    }
    if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS) {
        pitch += rotSpeed;
    }
    if (glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS) {
        pitch -= rotSpeed;
    }
    // Clamp pitch after using key rotations as well
    if (pitch > 89.0f)  pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;
}

int main() {
    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return -1;
    }
    // Configure GLFW context (OpenGL 3.3 Core Profile)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(800, 600, "Voxel Engine", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    // Capture the mouse cursor (disable OS cursor) for FPS camera
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // Initialize GLEW to load OpenGL function pointers
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW\n";
        glfwTerminate();
        return -1;
    }

    // Configure global OpenGL state
    glEnable(GL_DEPTH_TEST);             // enable depth testing (z-buffer):contentReference[oaicite:9]{index=9}
    glDepthFunc(GL_LESS);               // depth test passes if fragment depth is less
    glEnable(GL_CULL_FACE);             // enable face culling (don't draw back faces)
    glCullFace(GL_BACK);                // cull back-facing triangles
    glFrontFace(GL_CCW);                // CCW wound triangles are considered front-facing

    // Build and compile shaders (vertex and fragment)
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
            // Combine Model, View, Projection to transform vertex
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
        // Check compile status
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

    // Define colors for block types (Stone, Grass, Dirt)
    glm::vec3 colorStone(0.6f, 0.6f, 0.6f);  // gray
    glm::vec3 colorGrass(0.3f, 0.8f, 0.3f);  // green
    glm::vec3 colorDirt(0.59f, 0.29f, 0.0f); // brown

    // Create and initialize a Chunk
    Chunk chunk(0, 0, 0);
    // Fill chunk blocks and generate mesh geometry with per-face culling
    chunk.generate();
    // (Inside Chunk::generate, we populate chunk.m_blocks and build m_vertices for faces not obscured by neighbors.)
    // For example:
    //  - Set bottom layer (y=0) to Stone, y=1 to Dirt, y=2 to Grass, and the rest to Air.
    //  - Loop through all blocks; if a block is not Air, check each of the 6 neighbor positions.
    //    If the neighbor is out of bounds or is Air, add that face (two triangles) to m_vertices:contentReference[oaicite:10]{index=10}.
    //    Assign the face's color based on the block type (e.g., grass block faces get green).
    //  - Create the VAO/VBO, upload vertex positions and colors, and set vertex attrib pointers for location 0 and 1.
    // (This ensures only exposed faces contribute to the mesh, culling all interior faces:contentReference[oaicite:11]{index=11}.)

    // For demonstration, call chunk.generate() which we assume implements the above logic.

    // After generation, set up vertex attributes (position & color) in the VAO
    // (Assuming Chunk::generate bound the VAO and uploaded interleaved position/color data.)
    // By now, chunk.m_vao holds the mesh data for only visible faces of the blocks.

    // Retrieve uniform locations for the MVP matrices in the shader
    glUseProgram(shaderProgram);
    GLint modelLoc      = glGetUniformLocation(shaderProgram, "uModel");
    GLint viewLoc       = glGetUniformLocation(shaderProgram, "uView");
    GLint projectionLoc = glGetUniformLocation(shaderProgram, "uProjection");

    // Main render loop
    while (!glfwWindowShouldClose(window)) {
        // Per-frame time logic
        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // Process keyboard input for movement/rotation
        processInput(window);

        // Update camera direction vector from yaw and pitch:contentReference[oaicite:12]{index=12}
        glm::vec3 direction;
        direction.x = cos(glm::radians(pitch)) * cos(glm::radians(yaw));
        direction.y = sin(glm::radians(pitch));
        direction.z = cos(glm::radians(pitch)) * sin(glm::radians(yaw));
        cameraFront = glm::normalize(direction);

        // Prepare transformation matrices
        glm::mat4 model = glm::mat4(1.0f);
        // If the chunk had world position (m_x, m_y, m_z), apply translation: 
        // model = glm::translate(model, glm::vec3(chunk_xOffset, chunk_yOffset, chunk_zOffset));
        glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        float aspect = (float)width / (float)height;
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);

        // Clear screen to a sky-blue color and clear depth buffer
        glClearColor(0.529f, 0.808f, 0.922f, 1.0f);  // sky blue background
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Render chunk
        glUseProgram(shaderProgram);
        // Pass transformation matrices to the shader uniforms
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(projectionLoc, 1, GL_FALSE, glm::value_ptr(projection));
        // Draw the chunk geometry. Chunk::render will bind the VAO and issue the draw call.
        chunk.render(shaderProgram);
        // (Chunk::render is expected to call glDrawArrays with the vertex count of the generated mesh.)

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Cleanup and exit
    glfwTerminate();
    return 0;
}
