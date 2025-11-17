#pragma once

#include <vector>
#include <memory>
#include <random>
#include <mutex>
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
     * Generate a tree at the specified world position using biome's tree templates
     * @param world World to place tree in
     * @param x World X coordinate (base of tree)
     * @param y World Y coordinate (ground level)
     * @param z World Z coordinate (base of tree)
     * @param biome Biome to use tree templates from
     * @return true if tree was placed successfully
     */
    bool placeTree(World* world, int x, int y, int z, const struct Biome* biome);

    /**
     * Generate tree templates for a specific biome
     * Creates 10 unique tree templates with the biome's log and leaf blocks
     * @param biome Biome to generate templates for (modifies biome.tree_templates)
     */
    void generateTreeTemplatesForBiome(struct Biome* biome);

    /**
     * Get a random tree type index (0-9) based on rng
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
    std::mutex m_rngMutex;
};
