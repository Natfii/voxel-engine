#include "raycast.h"
#include "world.h"
#include "block_system.h"
#include <cmath>
#include <algorithm>

RaycastHit Raycast::castRay(World* world, const glm::vec3& origin, const glm::vec3& direction, float maxDistance) {
    RaycastHit result;
    result.hit = false;
    result.distance = 0.0f;

    // Null pointer check - return empty result if world is null
    if (world == nullptr) {
        return result;
    }

    // BUG FIX: Guard against zero-length direction vector
    // Prevents NaN from normalize() which would cause raycast to fail
    float dirLength = glm::length(direction);
    if (dirLength < 0.0001f) {
        return result;  // Invalid direction, return no hit
    }

    // Normalize direction
    glm::vec3 dir = glm::normalize(direction);

    // DDA (Digital Differential Analyzer) voxel traversal
    // Start position
    glm::vec3 pos = origin;

    // Step direction (1 or -1 for each axis)
    glm::ivec3 step(
        dir.x > 0 ? 1 : -1,
        dir.y > 0 ? 1 : -1,
        dir.z > 0 ? 1 : -1
    );

    // Delta distance: how far we travel along the ray to cross one voxel
    // Use epsilon to prevent divide-by-zero crashes when ray is axis-aligned
    const float epsilon = 0.0001f;
    glm::vec3 deltaDist(
        std::abs(1.0f / std::max(std::abs(dir.x), epsilon)),
        std::abs(1.0f / std::max(std::abs(dir.y), epsilon)),
        std::abs(1.0f / std::max(std::abs(dir.z), epsilon))
    );

    // Current voxel position (in block space, where 1 block = 1.0 units)
    glm::vec3 voxelPos = pos;
    glm::ivec3 mapPos(
        static_cast<int>(std::floor(voxelPos.x)),
        static_cast<int>(std::floor(voxelPos.y)),
        static_cast<int>(std::floor(voxelPos.z))
    );

    // Side distance: distance to next voxel boundary for each axis
    glm::vec3 sideDist;
    for (int i = 0; i < 3; ++i) {
        if (step[i] > 0) {
            sideDist[i] = (mapPos[i] + 1.0f - voxelPos[i]) * deltaDist[i];
        } else {
            sideDist[i] = (voxelPos[i] - mapPos[i]) * deltaDist[i];
        }
    }

    // DDA traversal
    glm::ivec3 normal(0, 0, 0);
    float totalDist = 0.0f;
    const float maxDist = maxDistance; // Already in block space

    while (totalDist < maxDist) {
        // Check current voxel
        glm::vec3 worldPos(static_cast<float>(mapPos.x), static_cast<float>(mapPos.y), static_cast<float>(mapPos.z));
        int blockID = world->getBlockAt(worldPos.x, worldPos.y, worldPos.z);

        if (isSolid(blockID)) { // Hit a solid block (not air)
            result.hit = true;
            result.position = worldPos;
            result.normal = glm::vec3(normal);
            result.blockX = mapPos.x;
            result.blockY = mapPos.y;
            result.blockZ = mapPos.z;
            result.distance = totalDist; // Already in world units
            return result;
        }

        // Step to next voxel
        if (sideDist.x < sideDist.y && sideDist.x < sideDist.z) {
            totalDist = sideDist.x;
            sideDist.x += deltaDist.x;
            mapPos.x += step.x;
            normal = glm::ivec3(-step.x, 0, 0);
        } else if (sideDist.y < sideDist.z) {
            totalDist = sideDist.y;
            sideDist.y += deltaDist.y;
            mapPos.y += step.y;
            normal = glm::ivec3(0, -step.y, 0);
        } else {
            totalDist = sideDist.z;
            sideDist.z += deltaDist.z;
            mapPos.z += step.z;
            normal = glm::ivec3(0, 0, -step.z);
        }
    }

    return result;
}
