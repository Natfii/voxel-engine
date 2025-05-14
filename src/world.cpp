#include "world.h"

World::World(int width, int height, int depth)
    : m_width(width), m_height(height), m_depth(depth) {
    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) {
            for (int z = 0; z < depth; ++z) {
                m_chunks.push_back(new Chunk(x, y, z));
            }
        }
    }
}

World::~World() {
    for (Chunk* chunk : m_chunks) {
        delete chunk;
    }
}

int World::index(int x, int y, int z) const {
    return x + m_width * (y + m_height * z);
}

void World::generateWorld() {
    for (Chunk* chunk : m_chunks) {
        chunk->generate();
    }
}

void World::renderWorld() {
    for (Chunk* chunk : m_chunks) {
        chunk->render();
    }
}
