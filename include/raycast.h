#pragma once

#include <glm/glm.hpp>

// Forward declaration
class World;

struct RaycastHit {
    bool hit;
    glm::vec3 position;     // World position of the hit block
    glm::vec3 normal;       // Normal of the face that was hit
    int blockX, blockY, blockZ; // Block coordinates
    float distance;         // Distance from ray origin to hit point
};

class Raycast {
public:
    // Cast a ray from origin in direction, returns first solid block hit
    // maxDistance is in world units (default 5 blocks = 2.5 units)
    static RaycastHit castRay(World* world, const glm::vec3& origin, const glm::vec3& direction, float maxDistance = 2.5f);
};
