// chunk.cpp
#include "chunk.h"
#include "block_system.h"   // for BlockRegistry and BlockDefinition
#include <vector>
#include <array>
#include <GL/glew.h>

// Constructor: Initialize chunk at world grid coordinates (x, y, z)
Chunk::Chunk(int x, int y, int z)
 : m_x(x), m_y(y), m_z(z), m_vao(0), m_vbo(0), m_vertexCount(0)
{
    // Determine block IDs from registry (fallback to Air=0 if not found)
    auto& registry = BlockRegistry::instance();
    int grassID = registry.getID("Grass");
    if (grassID < 0) grassID = 0;
    int dirtID = registry.getID("Dirt");
    if (dirtID < 0) dirtID = 0;
    int stoneID = registry.getID("Stone");
    if (stoneID < 0) stoneID = 0;
    // Fill m_blocks based on simple height rules:
    for(int X = 0; X < WIDTH;  ++X) {
        for(int Z = 0; Z < DEPTH;  ++Z) {
            for(int Y = 0; Y < HEIGHT; ++Y) {
                if      (Y == 8)                 m_blocks[X][Y][Z] = grassID;
                else if (Y < 8 && Y >= 6)       m_blocks[X][Y][Z] = dirtID;
                else if (Y < 6)                 m_blocks[X][Y][Z] = stoneID;
                else                            m_blocks[X][Y][Z] = 0; // Air (ID 0)
            }
        }
    }
}

Chunk::~Chunk() {
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
}

void Chunk::generate() {
    // Define a unit cube (36 vertices)
    static constexpr std::array<float, 108> cube = {{
        // front face (z = 0)
        0,0,0,  1,0,0,  1,1,0,   0,0,0,  1,1,0,  0,1,0,
        // back face (z = 1)
        1,0,1,  0,0,1,  0,1,1,   1,0,1,  0,1,1,  1,1,1,
        // left face (x = 0)
        0,0,1,  0,0,0,  0,1,0,   0,0,1,  0,1,0,  0,1,1,
        // right face (x = 1)
        1,0,0,  1,0,1,  1,1,1,   1,0,0,  1,1,1,  1,1,0,
        // top face (y = 1)
        0,1,0,  1,1,0,  1,1,1,   0,1,0,  1,1,1,  0,1,1,
        // bottom face (y = 0)
        0,0,1,  1,0,1,  1,0,0,   0,0,1,  1,0,0,  0,0,0
    }};

    std::vector<Vertex> verts;
    verts.reserve(WIDTH * HEIGHT * DEPTH * 36);

    // Iterate over every block in the chunk
    for(int X = 0; X < WIDTH;  ++X) {
        for(int Z = 0; Z < DEPTH;  ++Z) {
            for(int Y = 0; Y < HEIGHT; ++Y) {
                int id = m_blocks[X][Y][Z];
                if (id == 0) continue; // Skip air

                // Look up block definition by ID
                const BlockDefinition& def = BlockRegistry::instance().get(id);
                float cr, cg, cb;
                if (def.hasColor) {
                    // Use the block's defined color
                    cr = def.color.r;
                    cg = def.color.g;
                    cb = def.color.b;
                } else {
                    // No color defined (likely has a texture); use white
                    cr = cg = cb = 1.0f;
                }

                // Append vertices for all 6 faces of this block (36 vertices)
                for (int i = 0; i < 108; i += 3) {
                    Vertex v;
                    v.x = cube[i+0] + float(X);
                    v.y = cube[i+1] + float(Y);
                    v.z = cube[i+2] + float(Z);
                    v.r = cr;
                    v.g = cg;
                    v.b = cb;
                    verts.push_back(v);
                }
            }
        }
    }

    m_vertexCount = GLsizei(verts.size());

    // Upload to GPU
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

    // position @ attrib 0
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                          sizeof(Vertex),
                          (void*)offsetof(Vertex, x));
    // color @ attrib 1
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
