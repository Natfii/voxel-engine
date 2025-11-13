#include "tree_generator.h"
#include "world.h"
#include "terrain_constants.h"
#include <cmath>

TreeGenerator::TreeGenerator(int seed) : m_rng(seed + 9999) {
    // Seed offset to make trees different from terrain
}

void TreeGenerator::generateTreeTemplates(int logBlockID, int leavesBlockID) {
    m_templates.clear();
    m_templates.reserve(10);

    // Generate 10 different tree types
    for (int i = 0; i < 10; i++) {
        TreeTemplate tree;
        tree.type_name = "tree_" + std::to_string(i);

        if (i < 3) {
            // Small trees (height 4-6)
            generateSmallTree(tree, logBlockID, leavesBlockID);
        } else if (i < 7) {
            // Medium trees (height 7-10)
            generateMediumTree(tree, logBlockID, leavesBlockID);
        } else {
            // Large trees (height 11-15)
            generateLargeTree(tree, logBlockID, leavesBlockID);
        }

        m_templates.push_back(std::move(tree));
    }
}

bool TreeGenerator::placeTree(World* world, int blockX, int blockY, int blockZ, int treeType,
                                int logBlockID, int leavesBlockID) {
    if (treeType < 0 || treeType >= static_cast<int>(m_templates.size())) {
        return false;
    }

    const TreeTemplate& tree = m_templates[treeType];

    // Check if there's enough space (don't replace existing blocks except air/grass)
    for (const auto& block : tree.blocks) {
        int bx = blockX + block.offset.x;
        int by = blockY + block.offset.y;
        int bz = blockZ + block.offset.z;

        // Convert block coordinates to world coordinates (1 block = 0.5 world units)
        float worldX = bx * 0.5f;
        float worldY = by * 0.5f;
        float worldZ = bz * 0.5f;

        int existingBlock = world->getBlockAt(worldX, worldY, worldZ);

        // Allow placing over air and grass
        if (existingBlock != TerrainGeneration::BLOCK_AIR &&
            existingBlock != TerrainGeneration::BLOCK_GRASS) {
            return false;  // Can't place tree here
        }
    }

    // Place all tree blocks
    for (const auto& block : tree.blocks) {
        int bx = blockX + block.offset.x;
        int by = blockY + block.offset.y;
        int bz = blockZ + block.offset.z;

        // Convert block coordinates to world coordinates
        float worldX = bx * 0.5f;
        float worldY = by * 0.5f;
        float worldZ = bz * 0.5f;

        world->setBlockAt(worldX, worldY, worldZ, block.blockID);
    }

    return true;
}

int TreeGenerator::getRandomTreeType() {
    if (m_templates.empty()) {
        return 0;
    }
    std::uniform_int_distribution<int> dist(0, static_cast<int>(m_templates.size()) - 1);
    return dist(m_rng);
}

// ==================== Tree Generation Functions ====================

void TreeGenerator::generateSmallTree(TreeTemplate& tree, int logID, int leavesID) {
    // Small tree: 4-6 blocks tall
    std::uniform_int_distribution<int> heightDist(4, 6);
    int height = heightDist(m_rng);
    tree.height = height;

    // Add trunk
    addTrunk(tree, height, logID);

    // Add simple canopy at top
    addCanopy(tree, height, 2, leavesID);
}

void TreeGenerator::generateMediumTree(TreeTemplate& tree, int logID, int leavesID) {
    // Medium tree: 7-10 blocks tall with some branches
    std::uniform_int_distribution<int> heightDist(7, 10);
    int height = heightDist(m_rng);
    tree.height = height;

    // Add trunk
    addTrunk(tree, height, logID);

    // Add canopy
    addCanopy(tree, height, 3, leavesID);

    // Add a few small branches
    int branchHeight = height - 3;
    for (int i = 0; i < 3; i++) {
        glm::ivec3 start(0, branchHeight, 0);
        std::uniform_int_distribution<int> dirDist(0, 3);
        int dir = dirDist(m_rng);

        glm::ivec3 direction;
        switch (dir) {
            case 0: direction = glm::ivec3(1, 0, 0); break;
            case 1: direction = glm::ivec3(-1, 0, 0); break;
            case 2: direction = glm::ivec3(0, 0, 1); break;
            case 3: direction = glm::ivec3(0, 0, -1); break;
        }

        addBranch(tree, start, direction, 2, 0, logID, leavesID);
        branchHeight++;
    }
}

void TreeGenerator::generateLargeTree(TreeTemplate& tree, int logID, int leavesID) {
    // Large tree: 11-15 blocks tall with fractal branching
    std::uniform_int_distribution<int> heightDist(11, 15);
    int height = heightDist(m_rng);
    tree.height = height;

    // Add thick trunk (2x2 base for very tall trees)
    if (height >= 13) {
        for (int y = 0; y < height; y++) {
            tree.blocks.push_back({{0, y, 0}, logID});
            tree.blocks.push_back({{1, y, 0}, logID});
            tree.blocks.push_back({{0, y, 1}, logID});
            tree.blocks.push_back({{1, y, 1}, logID});
        }
    } else {
        addTrunk(tree, height, logID);
    }

    // Add large canopy
    addCanopy(tree, height, 4, leavesID);

    // Add multiple layers of branches using fractal method
    int branchStart = height * 2 / 3;
    for (int y = branchStart; y < height - 1; y += 2) {
        // 4 branches in cardinal directions
        glm::ivec3 start(0, y, 0);

        addBranch(tree, start, glm::ivec3(1, 1, 0), 4, 1, logID, leavesID);
        addBranch(tree, start, glm::ivec3(-1, 1, 0), 4, 1, logID, leavesID);
        addBranch(tree, start, glm::ivec3(0, 1, 1), 4, 1, logID, leavesID);
        addBranch(tree, start, glm::ivec3(0, 1, -1), 4, 1, logID, leavesID);
    }
}

// ==================== Helper Functions ====================

void TreeGenerator::addTrunk(TreeTemplate& tree, int height, int logID) {
    for (int y = 0; y < height; y++) {
        tree.blocks.push_back({{0, y, 0}, logID});
    }
}

void TreeGenerator::addCanopy(TreeTemplate& tree, int trunkHeight, int radius, int leavesID) {
    // Spherical canopy centered at top of trunk
    glm::ivec3 center(0, trunkHeight - 1, 0);

    for (int x = -radius; x <= radius; x++) {
        for (int y = -radius; y <= radius; y++) {
            for (int z = -radius; z <= radius; z++) {
                // Skip trunk center
                if (x == 0 && z == 0 && y <= 1) continue;

                // Check if within sphere
                float dist = std::sqrt(x*x + y*y + z*z);
                if (dist <= radius) {
                    glm::ivec3 pos = center + glm::ivec3(x, y, z);
                    tree.blocks.push_back({pos, leavesID});
                }
            }
        }
    }
}

void TreeGenerator::addBranch(TreeTemplate& tree, glm::ivec3 start, glm::ivec3 direction,
                               int length, int depth, int logID, int leavesID) {
    if (length <= 0 || depth > 2) {
        return;  // Max recursion depth = 2
    }

    // Place branch blocks
    glm::ivec3 pos = start;
    for (int i = 0; i < length; i++) {
        tree.blocks.push_back({pos, logID});

        // Move to next position
        pos += direction;

        // Add leaves around branch tip
        if (i >= length - 2) {
            tree.blocks.push_back({pos + glm::ivec3(0, 1, 0), leavesID});
            tree.blocks.push_back({pos + glm::ivec3(1, 0, 0), leavesID});
            tree.blocks.push_back({pos + glm::ivec3(-1, 0, 0), leavesID});
            tree.blocks.push_back({pos + glm::ivec3(0, 0, 1), leavesID});
            tree.blocks.push_back({pos + glm::ivec3(0, 0, -1), leavesID});
        }
    }

    // Recursively add sub-branches (fractal)
    if (depth < 2 && length > 2) {
        std::uniform_int_distribution<int> branchDist(0, 1);
        if (branchDist(m_rng) == 0) {
            // Add perpendicular sub-branch
            glm::ivec3 perpDir;
            if (direction.x != 0) {
                perpDir = glm::ivec3(0, 1, 1);
            } else if (direction.z != 0) {
                perpDir = glm::ivec3(1, 1, 0);
            } else {
                perpDir = glm::ivec3(1, 0, 1);
            }

            glm::ivec3 midpoint = start + direction * (length / 2);
            addBranch(tree, midpoint, perpDir, length / 2, depth + 1, logID, leavesID);
        }
    }
}
