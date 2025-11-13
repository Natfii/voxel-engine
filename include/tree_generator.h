#pragma once

#include <vector>
#include <memory>
#include <random>
#include <glm/glm.hpp>

// Forward declaration
class World;

/**
 * Represents a single block in a tree structure
 */
struct TreeBlock {
    glm::ivec3 offset;  // Offset from tree origin
    int blockID;        // Block ID (log or leaves)
};

/**
 * Represents a procedurally generated tree template
 */
struct TreeTemplate {
    int height;                      // Tree height in blocks
    std::vector<TreeBlock> blocks;   // All blocks that make up the tree
    std::string type_name;           // "small_oak", "large_oak", etc.
};

/**
 * Generates procedural trees using fractal/L-system-inspired methods
 *
 * Based on the Instructables procedural tree generation article.
 * Creates varied tree structures with natural branching patterns.
 */
class TreeGenerator {
public:
    TreeGenerator(int seed);

    /**
     * Generate a tree at the specified world position
     * @param world World to place tree in
     * @param x World X coordinate (base of tree)
     * @param y World Y coordinate (ground level)
     * @param z World Z coordinate (base of tree)
     * @param treeType Tree template index (0-9)
     * @param logBlockID Block ID for trunk/branches
     * @param leavesBlockID Block ID for leaves
     * @return true if tree was placed successfully
     */
    bool placeTree(World* world, int x, int y, int z, int treeType,
                   int logBlockID, int leavesBlockID);

    /**
     * Generate all tree templates (10 types of varying sizes)
     * Called once during world initialization
     */
    void generateTreeTemplates(int logBlockID, int leavesBlockID);

    /**
     * Get a random tree type index based on rng
     */
    int getRandomTreeType();

private:
    // Generate a specific tree template
    void generateSmallTree(TreeTemplate& tree, int logID, int leavesID);
    void generateMediumTree(TreeTemplate& tree, int logID, int leavesID);
    void generateLargeTree(TreeTemplate& tree, int logID, int leavesID);

    // Helper: Add trunk blocks
    void addTrunk(TreeTemplate& tree, int height, int logID);

    // Helper: Add leaf canopy
    void addCanopy(TreeTemplate& tree, int trunkHeight, int radius, int leavesID);

    // Helper: Add fractal branches (recursive)
    void addBranch(TreeTemplate& tree, glm::ivec3 start, glm::ivec3 direction,
                   int length, int depth, int logID, int leavesID);

private:
    std::mt19937 m_rng;
    std::vector<TreeTemplate> m_templates;  // 10 tree templates
};
