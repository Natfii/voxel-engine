#pragma once
#include <vector>
#include "chunk.h"

class World {
public:
    World(int width, int height, int depth);
    ~World();

    void generateWorld();
    void renderWorld();

private:
    int m_width, m_height, m_depth;
    std::vector<Chunk*> m_chunks;

    int index(int x, int y, int z) const;
};
