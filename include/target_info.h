#pragma once

#include <glm/glm.hpp>
#include <string>

// Rich information about what the player is targeting
struct TargetInfo {
    // Raycast results
    bool hasTarget;
    glm::vec3 blockPosition;     // World coordinates of the block
    glm::vec3 hitNormal;         // Normal of the face that was hit
    glm::ivec3 blockCoords;      // Block grid coordinates
    float distance;              // Distance from player to block

    // Block data
    int blockID;
    std::string blockName;       // "Stone", "Grass", "Dirt", etc.
    std::string blockType;       // "solid", "liquid", "transparent", "air"
    bool isBreakable;

    // Future: Entity support
    // EntityID entityID;
    // std::string entityName;
    // EntityType entityType;

    // Constructor
    TargetInfo()
        : hasTarget(false),
          blockPosition(0.0f),
          hitNormal(0.0f),
          blockCoords(0),
          distance(0.0f),
          blockID(0),
          blockName(""),
          blockType("air"),
          isBreakable(false) {
    }

    // Helper: Get position where a new block should be placed (adjacent to hit face)
    glm::vec3 getPlacementPosition() const {
        return blockPosition + (hitNormal * 0.5f);
    }

    // Helper: Get block grid coordinates for placement
    glm::ivec3 getPlacementCoords() const {
        glm::vec3 placementPos = getPlacementPosition();
        return glm::ivec3(
            static_cast<int>(std::floor(placementPos.x / 0.5f)),
            static_cast<int>(std::floor(placementPos.y / 0.5f)),
            static_cast<int>(std::floor(placementPos.z / 0.5f))
        );
    }

    // Helper: Check if target is valid for interaction
    bool isValid() const {
        return hasTarget && blockID > 0; // Not air
    }
};
