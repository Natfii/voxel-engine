#include "player.h"
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>

Player::Player(glm::vec3 position, glm::vec3 up, float yaw, float pitch)
    : Position(position), WorldUp(up), Yaw(yaw), Pitch(pitch),
      MovementSpeed(5.0f), MouseSensitivity(0.1f), FirstMouse(true)
{
    updateVectors();
}

void Player::update(GLFWwindow* window, float deltaTime) {
    float velocity = MovementSpeed * deltaTime;

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        Position += Front * velocity;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        Position -= Front * velocity;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        Position -= Right * velocity;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        Position += Right * velocity;
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
        Position += WorldUp * velocity;
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        Position -= WorldUp * velocity;

    if (glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED) {
        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);

        if (FirstMouse) {
            LastX = float(xpos);
            LastY = float(ypos);
            FirstMouse = false;
        }

        float xoffset = float(xpos - LastX);
        float yoffset = float(LastY - ypos); // Reversed for Vulkan's flipped Y projection
        LastX = float(xpos);
        LastY = float(ypos);

        xoffset *= MouseSensitivity;
        yoffset *= MouseSensitivity;

        Yaw   += xoffset;
        Pitch += yoffset;

        if (Pitch > 89.0f)
            Pitch = 89.0f;
        if (Pitch < -89.0f)
            Pitch = -89.0f;

        updateVectors();
    }
}

glm::mat4 Player::getViewMatrix() const {
    return glm::lookAt(Position, Position + Front, Up);
}

void Player::updateVectors() {
    glm::vec3 front;
    front.x = cos(glm::radians(Yaw)) * cos(glm::radians(Pitch));
    front.y = sin(glm::radians(Pitch));
    front.z = sin(glm::radians(Yaw)) * cos(glm::radians(Pitch));
    Front = glm::normalize(front);

    Right = glm::normalize(glm::cross(Front, WorldUp));
    Up    = glm::normalize(glm::cross(Right, Front));
}

void Player::resetMouse() {
    FirstMouse = true;
}
