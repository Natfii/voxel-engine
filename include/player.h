#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <GLFW/glfw3.h>

class Player {
public:
    Player(glm::vec3 position, glm::vec3 up, float yaw, float pitch);

    void resetMouse();
    void update(GLFWwindow* window, float deltaTime);
    glm::mat4 getViewMatrix() const;

    glm::vec3 Position;
    glm::vec3 Front;
    glm::vec3 Up;
    glm::vec3 Right;
    glm::vec3 WorldUp;

    float Yaw;
    float Pitch;
    float MovementSpeed;
    float MouseSensitivity;

private:
    bool FirstMouse;
    float LastX;
    float LastY;

    void updateVectors();
};