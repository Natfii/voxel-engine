#pragma once

#include <vector>
#include <GL/glew.h>
#include "voxelmath.h"

struct Vertex {
    float x, y, z;
    float r, g, b;
};

class Chunk {
public:
    static const int WIDTH = 16;
    static const int HEIGHT = 16;
    static const int DEPTH = 16;

    Chunk(int x, int y, int z);
    ~Chunk();

    void generate();
    void render(); 

private:
    int m_x, m_y, m_z;
    int m_blocks[WIDTH][HEIGHT][DEPTH];  
    std::vector<Vertex> m_vertices;
    GLuint m_vao, m_vbo;
    GLsizei m_vertexCount;
};