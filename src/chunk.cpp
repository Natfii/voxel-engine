#include "chunk.h"
#include <vector>
#include <array>
#include <GL/glew.h>

// Constructor: Initialize chunk at world grid coordinates (x, y, z)
Chunk::Chunk(int x, int y, int z)
 : m_x(x), m_y(y), m_z(z), m_vao(0), m_vbo(0), m_vertexCount(0)
{
    // Simple flat terrain generation:
    //  - If Y == 8: Grass
    //  - If 6 <= Y < 8: Dirt
    //  - If Y < 6: Stone
    //  - If Y > 8: Air
    // You can change these rules to generate different terrain shapes (or use noise).
    //Change height thresholds (e.g. make grass at Y == 10 instead of 8).
    //Use random or noise (Perlin/simplex) to set m_blocks[X][Y][Z] for hills and valleys.
    //Add water/lava levels by inserting another else if (Y < waterLevel) m_blocks = Block_Water;.
    //Carve caves by randomly setting some blocks to Block_Air.

    for(int X = 0; X < WIDTH;  ++X)
    for(int Z = 0; Z < DEPTH;  ++Z)
    for(int Y = 0; Y < HEIGHT; ++Y) {
        if      (Y == 8)           m_blocks[X][Y][Z] = Block_Grass;
        else if (Y < 8 && Y >= 6)  m_blocks[X][Y][Z] = Block_Dirt;
        else if (Y < 6)            m_blocks[X][Y][Z] = Block_Stone;
        else                       m_blocks[X][Y][Z] = Block_Air;
    }
}

Chunk::~Chunk() {
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
}

void Chunk::generate() {
    // Define a unit cube with 36 vertices (6 faces × 2 triangles per face × 3 vertices per triangle).
    // Each group of 6 vertices below represents one face of the cube.
    static constexpr std::array<float, 108> cube = {{
        // front face (z = 0)
        0,0,0,  1,0,0,  1,1,0,    0,0,0,  1,1,0,  0,1,0,
        // back face (z = 1)
        1,0,1,  0,0,1,  0,1,1,    1,0,1,  0,1,1,  1,1,1,
        // left face (x = 0)
        0,0,1,  0,0,0,  0,1,0,    0,0,1,  0,1,0,  0,1,1,
        // right face (x = 1)
        1,0,0,  1,0,1,  1,1,1,    1,0,0,  1,1,1,  1,1,0,
        // top face (y = 1)
        0,1,0,  1,1,0,  1,1,1,    0,1,0,  1,1,1,  0,1,1,
        // bottom face (y = 0)
        0,0,1,  1,0,1,  1,0,0,    0,0,1,  1,0,0,  0,0,0
    }};

    std::vector<Vertex> verts;
    // Reserve enough space: worst case all blocks are solid
    verts.reserve(WIDTH * HEIGHT * DEPTH * 36);

    // Lambda to map block types to colors
    auto colorFor = [&](BlockType t) {
        switch(t) {
            case Block_Stone: return std::array<float,3>{0.5f, 0.5f, 0.5f};   // gray
            case Block_Dirt:  return std::array<float,3>{0.59f, 0.29f, 0.0f};  // brown
            case Block_Grass: return std::array<float,3>{0.1f, 0.8f, 0.1f};    // green
            default:          return std::array<float,3>{0.0f, 0.0f, 0.0f};    // air (unused)
        }
    };

    // Iterate over every block in the chunk
    for(int X = 0; X < WIDTH;  ++X) {
        for(int Z = 0; Z < DEPTH;  ++Z) {
            for(int Y = 0; Y < HEIGHT; ++Y) {
                BlockType t = m_blocks[X][Y][Z];
                if (t == Block_Air) continue; // Skip empty space

                auto col = colorFor(t);      // Get color for this block type

                // Append vertices for all 6 faces of this block (36 vertices)
                // (Currently, this includes internal faces between adjacent blocks.)
                for (int i = 0; i < 108; i += 3) {
                    Vertex v;
                    // Position: cube vertex offset plus block coordinates
                    v.x = cube[i+0] + float(X);
                    v.y = cube[i+1] + float(Y);
                    v.z = cube[i+2] + float(Z);
                    // Color: same for all vertices of this block
                    v.r = col[0];
                    v.g = col[1];
                    v.b = col[2];
                    verts.push_back(v);
                }
            }
        }
    }

    m_vertexCount = GLsizei(verts.size()); // Total number of vertices for glDrawArrays

// upload to GPU
    if (!m_vao) {
        glGenVertexArrays(1, &m_vao);
        glGenBuffers     (1, &m_vbo);
    }
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 verts.size() * sizeof(Vertex),
                 verts.data(),
                 GL_STATIC_DRAW);

    // position @ attrib 0 (3 floats)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                          sizeof(Vertex),
                          (void*)offsetof(Vertex, x));

    // color    @ attrib 1 (3 floats), right after xyz
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                          sizeof(Vertex),
                          (void*)offsetof(Vertex, r));

    glBindVertexArray(0);
}

void Chunk::render() {
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, m_vertexCount);
    glBindVertexArray(0);
}
