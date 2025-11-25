/**
 * @file mesh_loader.cpp
 * @brief Implementation of mesh loading from various formats
 */

#include "mesh/mesh_loader.h"
#include "logger.h"
#include <glm/gtc/constants.hpp>  // For glm::pi
#include <fstream>
#include <sstream>
#include <cmath>
#include <unordered_map>

// ========== OBJ Loader ==========

std::vector<Mesh> MeshLoader::loadOBJ(const std::string& filepath,
                                       std::vector<PBRMaterial>& materials) {
    Logger::info() << "Loading OBJ mesh: " << filepath;

    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open OBJ file: " + filepath);
    }

    // Temporary storage for OBJ data (indexed separately)
    std::vector<glm::vec3> positions;
    std::vector<glm::vec2> uvs;
    std::vector<glm::vec3> normals;

    // Final mesh data (unique vertices)
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;

    // Map from (pos/uv/normal) triplet to vertex index
    std::unordered_map<std::string, uint32_t> vertexMap;

    std::string line;
    int lineNum = 0;

    while (std::getline(file, line)) {
        lineNum++;

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::istringstream iss(line);
        std::string prefix;
        iss >> prefix;

        if (prefix == "v") {
            // Vertex position: v x y z
            glm::vec3 pos;
            iss >> pos.x >> pos.y >> pos.z;
            positions.push_back(pos);

        } else if (prefix == "vt") {
            // Texture coordinate: vt u v
            glm::vec2 uv;
            iss >> uv.x >> uv.y;
            uvs.push_back(uv);

        } else if (prefix == "vn") {
            // Vertex normal: vn x y z
            glm::vec3 normal;
            iss >> normal.x >> normal.y >> normal.z;
            normals.push_back(glm::normalize(normal));

        } else if (prefix == "f") {
            // Face: f v1/vt1/vn1 v2/vt2/vn2 v3/vt3/vn3
            std::vector<uint32_t> faceIndices;
            std::string vertexStr;

            while (iss >> vertexStr) {
                int posIdx = -1, uvIdx = -1, normalIdx = -1;
                if (!parseOBJFace(vertexStr, posIdx, uvIdx, normalIdx)) {
                    Logger::warning() << "Invalid face format at line " << lineNum << ": " << vertexStr;
                    continue;
                }

                // Convert to positive indices
                posIdx = convertOBJIndex(posIdx, positions.size());
                uvIdx = convertOBJIndex(uvIdx, uvs.size());
                normalIdx = convertOBJIndex(normalIdx, normals.size());

                // Check if this vertex combination already exists
                std::string vertexKey = std::to_string(posIdx) + "/" +
                                       std::to_string(uvIdx) + "/" +
                                       std::to_string(normalIdx);

                auto it = vertexMap.find(vertexKey);
                if (it != vertexMap.end()) {
                    // Vertex already exists, reuse index
                    faceIndices.push_back(it->second);
                } else {
                    // Create new vertex
                    MeshVertex vertex{};

                    // Position (required)
                    if (posIdx >= 0 && posIdx < static_cast<int>(positions.size())) {
                        vertex.position = positions[posIdx];
                    }

                    // UV (optional)
                    if (uvIdx >= 0 && uvIdx < static_cast<int>(uvs.size())) {
                        vertex.texCoord = uvs[uvIdx];
                    } else {
                        vertex.texCoord = glm::vec2(0.0f);
                    }

                    // Normal (optional, will calculate if missing)
                    if (normalIdx >= 0 && normalIdx < static_cast<int>(normals.size())) {
                        vertex.normal = normals[normalIdx];
                    } else {
                        vertex.normal = glm::vec3(0, 1, 0);  // Default up
                    }

                    // Tangent will be calculated later
                    vertex.tangent = glm::vec3(1, 0, 0);

                    uint32_t newIndex = static_cast<uint32_t>(vertices.size());
                    vertices.push_back(vertex);
                    vertexMap[vertexKey] = newIndex;
                    faceIndices.push_back(newIndex);
                }
            }

            // Triangulate face (assumes convex polygon)
            for (size_t i = 1; i + 1 < faceIndices.size(); i++) {
                indices.push_back(faceIndices[0]);
                indices.push_back(faceIndices[i]);
                indices.push_back(faceIndices[i + 1]);
            }
        }
    }

    file.close();

    if (vertices.empty() || indices.empty()) {
        throw std::runtime_error("OBJ file contains no valid geometry: " + filepath);
    }

    // Calculate normals if not provided
    bool hasNormals = false;
    for (const auto& v : vertices) {
        if (glm::length(v.normal) > 0.1f) {
            hasNormals = true;
            break;
        }
    }

    if (!hasNormals) {
        Logger::info() << "Calculating normals for mesh (not provided in OBJ)";
        // Calculate face normals and average per vertex
        std::vector<glm::vec3> vertexNormals(vertices.size(), glm::vec3(0.0f));

        for (size_t i = 0; i < indices.size(); i += 3) {
            uint32_t i0 = indices[i];
            uint32_t i1 = indices[i + 1];
            uint32_t i2 = indices[i + 2];

            glm::vec3 edge1 = vertices[i1].position - vertices[i0].position;
            glm::vec3 edge2 = vertices[i2].position - vertices[i0].position;
            glm::vec3 faceNormal = glm::normalize(glm::cross(edge1, edge2));

            vertexNormals[i0] += faceNormal;
            vertexNormals[i1] += faceNormal;
            vertexNormals[i2] += faceNormal;
        }

        for (size_t i = 0; i < vertices.size(); i++) {
            if (glm::length(vertexNormals[i]) > 0.0001f) {
                vertices[i].normal = glm::normalize(vertexNormals[i]);
            }
        }
    }

    // Create mesh
    Mesh mesh("mesh", vertices, indices);
    mesh.calculateTangents();
    mesh.calculateBounds();

    // Create default material
    materials.clear();
    materials.push_back(PBRMaterial::createDefault());

    Logger::info() << "Loaded OBJ mesh: " << vertices.size() << " vertices, "
                  << indices.size() / 3 << " triangles";

    return { mesh };
}

bool MeshLoader::parseOBJFace(const std::string& faceStr,
                               int& vertexIdx, int& uvIdx, int& normalIdx) {
    vertexIdx = -1;
    uvIdx = -1;
    normalIdx = -1;

    // Format: v, v/vt, v/vt/vn, v//vn
    size_t pos = 0;
    size_t slash1 = faceStr.find('/', pos);
    size_t slash2 = faceStr.find('/', slash1 + 1);

    try {
        // Parse vertex index (always present)
        if (slash1 == std::string::npos) {
            // Format: v
            vertexIdx = std::stoi(faceStr);
        } else {
            // Format: v/... or v//...
            vertexIdx = std::stoi(faceStr.substr(0, slash1));

            if (slash2 == std::string::npos) {
                // Format: v/vt
                if (slash1 + 1 < faceStr.size()) {
                    uvIdx = std::stoi(faceStr.substr(slash1 + 1));
                }
            } else {
                // Format: v/vt/vn or v//vn
                if (slash2 > slash1 + 1) {
                    uvIdx = std::stoi(faceStr.substr(slash1 + 1, slash2 - slash1 - 1));
                }
                if (slash2 + 1 < faceStr.size()) {
                    normalIdx = std::stoi(faceStr.substr(slash2 + 1));
                }
            }
        }
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

int MeshLoader::convertOBJIndex(int index, size_t size) {
    if (index < 0) {
        // Negative index: count from end
        return static_cast<int>(size) + index;
    } else if (index > 0) {
        // Positive index: OBJ is 1-based, convert to 0-based
        return index - 1;
    }
    return -1;  // Invalid
}

// ========== glTF Loader (Stub) ==========

std::vector<Mesh> MeshLoader::loadGLTF(const std::string& filepath,
                                        std::vector<PBRMaterial>& materials) {
    throw std::runtime_error("glTF loading not yet implemented (Phase 2 feature)");
}

// ========== Procedural Mesh Generators ==========

Mesh MeshLoader::createCube(float size) {
    float s = size * 0.5f;

    std::vector<MeshVertex> vertices = {
        // Front face (+Z)
        {{-s, -s,  s}, {0, 0, 1}, {0, 0}, {1, 0, 0}},
        {{ s, -s,  s}, {0, 0, 1}, {1, 0}, {1, 0, 0}},
        {{ s,  s,  s}, {0, 0, 1}, {1, 1}, {1, 0, 0}},
        {{-s,  s,  s}, {0, 0, 1}, {0, 1}, {1, 0, 0}},

        // Back face (-Z)
        {{ s, -s, -s}, {0, 0, -1}, {0, 0}, {-1, 0, 0}},
        {{-s, -s, -s}, {0, 0, -1}, {1, 0}, {-1, 0, 0}},
        {{-s,  s, -s}, {0, 0, -1}, {1, 1}, {-1, 0, 0}},
        {{ s,  s, -s}, {0, 0, -1}, {0, 1}, {-1, 0, 0}},

        // Right face (+X)
        {{ s, -s,  s}, {1, 0, 0}, {0, 0}, {0, 0, -1}},
        {{ s, -s, -s}, {1, 0, 0}, {1, 0}, {0, 0, -1}},
        {{ s,  s, -s}, {1, 0, 0}, {1, 1}, {0, 0, -1}},
        {{ s,  s,  s}, {1, 0, 0}, {0, 1}, {0, 0, -1}},

        // Left face (-X)
        {{-s, -s, -s}, {-1, 0, 0}, {0, 0}, {0, 0, 1}},
        {{-s, -s,  s}, {-1, 0, 0}, {1, 0}, {0, 0, 1}},
        {{-s,  s,  s}, {-1, 0, 0}, {1, 1}, {0, 0, 1}},
        {{-s,  s, -s}, {-1, 0, 0}, {0, 1}, {0, 0, 1}},

        // Top face (+Y)
        {{-s,  s,  s}, {0, 1, 0}, {0, 0}, {1, 0, 0}},
        {{ s,  s,  s}, {0, 1, 0}, {1, 0}, {1, 0, 0}},
        {{ s,  s, -s}, {0, 1, 0}, {1, 1}, {1, 0, 0}},
        {{-s,  s, -s}, {0, 1, 0}, {0, 1}, {1, 0, 0}},

        // Bottom face (-Y)
        {{-s, -s, -s}, {0, -1, 0}, {0, 0}, {1, 0, 0}},
        {{ s, -s, -s}, {0, -1, 0}, {1, 0}, {1, 0, 0}},
        {{ s, -s,  s}, {0, -1, 0}, {1, 1}, {1, 0, 0}},
        {{-s, -s,  s}, {0, -1, 0}, {0, 1}, {1, 0, 0}}
    };

    std::vector<uint32_t> indices = {
        0, 1, 2,  2, 3, 0,   // Front
        4, 5, 6,  6, 7, 4,   // Back
        8, 9, 10, 10, 11, 8, // Right
        12, 13, 14, 14, 15, 12, // Left
        16, 17, 18, 18, 19, 16, // Top
        20, 21, 22, 22, 23, 20  // Bottom
    };

    Mesh mesh("cube", vertices, indices);
    mesh.calculateBounds();
    return mesh;
}

Mesh MeshLoader::createSphere(float radius, int segments) {
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;

    // Generate vertices
    for (int lat = 0; lat <= segments; lat++) {
        float theta = static_cast<float>(lat) * glm::pi<float>() / segments;
        float sinTheta = std::sin(theta);
        float cosTheta = std::cos(theta);

        for (int lon = 0; lon <= segments; lon++) {
            float phi = static_cast<float>(lon) * 2.0f * glm::pi<float>() / segments;
            float sinPhi = std::sin(phi);
            float cosPhi = std::cos(phi);

            MeshVertex vertex{};
            vertex.normal = glm::vec3(cosPhi * sinTheta, cosTheta, sinPhi * sinTheta);
            vertex.position = radius * vertex.normal;
            vertex.texCoord = glm::vec2(static_cast<float>(lon) / segments,
                                        static_cast<float>(lat) / segments);

            // Calculate tangent (perpendicular to normal)
            vertex.tangent = glm::normalize(glm::vec3(-sinPhi, 0, cosPhi));

            vertices.push_back(vertex);
        }
    }

    // Generate indices
    for (int lat = 0; lat < segments; lat++) {
        for (int lon = 0; lon < segments; lon++) {
            uint32_t first = lat * (segments + 1) + lon;
            uint32_t second = first + segments + 1;

            indices.push_back(first);
            indices.push_back(second);
            indices.push_back(first + 1);

            indices.push_back(second);
            indices.push_back(second + 1);
            indices.push_back(first + 1);
        }
    }

    Mesh mesh("sphere", vertices, indices);
    mesh.calculateBounds();
    return mesh;
}

Mesh MeshLoader::createCylinder(float radius, float height, int segments) {
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;

    float halfHeight = height * 0.5f;

    // Generate side vertices
    for (int i = 0; i <= segments; i++) {
        float angle = static_cast<float>(i) * 2.0f * glm::pi<float>() / segments;
        float cosAngle = std::cos(angle);
        float sinAngle = std::sin(angle);

        glm::vec3 normal(cosAngle, 0, sinAngle);
        glm::vec3 tangent(-sinAngle, 0, cosAngle);

        // Bottom vertex
        MeshVertex bottom{};
        bottom.position = glm::vec3(radius * cosAngle, -halfHeight, radius * sinAngle);
        bottom.normal = normal;
        bottom.tangent = tangent;
        bottom.texCoord = glm::vec2(static_cast<float>(i) / segments, 0);
        vertices.push_back(bottom);

        // Top vertex
        MeshVertex top{};
        top.position = glm::vec3(radius * cosAngle, halfHeight, radius * sinAngle);
        top.normal = normal;
        top.tangent = tangent;
        top.texCoord = glm::vec2(static_cast<float>(i) / segments, 1);
        vertices.push_back(top);
    }

    // Generate side indices
    for (int i = 0; i < segments; i++) {
        uint32_t bottomLeft = i * 2;
        uint32_t bottomRight = (i + 1) * 2;
        uint32_t topLeft = bottomLeft + 1;
        uint32_t topRight = bottomRight + 1;

        indices.push_back(bottomLeft);
        indices.push_back(bottomRight);
        indices.push_back(topLeft);

        indices.push_back(topLeft);
        indices.push_back(bottomRight);
        indices.push_back(topRight);
    }

    // Add caps (simplified - center vertex + ring)
    uint32_t baseIndex = static_cast<uint32_t>(vertices.size());

    // Bottom cap
    MeshVertex bottomCenter{};
    bottomCenter.position = glm::vec3(0, -halfHeight, 0);
    bottomCenter.normal = glm::vec3(0, -1, 0);
    bottomCenter.tangent = glm::vec3(1, 0, 0);
    bottomCenter.texCoord = glm::vec2(0.5f, 0.5f);
    vertices.push_back(bottomCenter);

    for (int i = 0; i < segments; i++) {
        indices.push_back(baseIndex);
        indices.push_back(i * 2);
        indices.push_back(((i + 1) % segments) * 2);
    }

    // Top cap
    MeshVertex topCenter{};
    topCenter.position = glm::vec3(0, halfHeight, 0);
    topCenter.normal = glm::vec3(0, 1, 0);
    topCenter.tangent = glm::vec3(1, 0, 0);
    topCenter.texCoord = glm::vec2(0.5f, 0.5f);
    vertices.push_back(topCenter);

    uint32_t topCenterIdx = static_cast<uint32_t>(vertices.size()) - 1;
    for (int i = 0; i < segments; i++) {
        indices.push_back(topCenterIdx);
        indices.push_back(((i + 1) % segments) * 2 + 1);
        indices.push_back(i * 2 + 1);
    }

    Mesh mesh("cylinder", vertices, indices);
    mesh.calculateBounds();
    return mesh;
}

Mesh MeshLoader::createPlane(float width, float depth, int subdivisions) {
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;

    float halfWidth = width * 0.5f;
    float halfDepth = depth * 0.5f;
    int verticesPerRow = subdivisions + 1;

    // Generate vertices
    for (int z = 0; z <= subdivisions; z++) {
        for (int x = 0; x <= subdivisions; x++) {
            float fx = static_cast<float>(x) / subdivisions;
            float fz = static_cast<float>(z) / subdivisions;

            MeshVertex vertex{};
            vertex.position = glm::vec3(
                -halfWidth + fx * width,
                0.0f,
                -halfDepth + fz * depth
            );
            vertex.normal = glm::vec3(0, 1, 0);
            vertex.tangent = glm::vec3(1, 0, 0);
            vertex.texCoord = glm::vec2(fx, fz);

            vertices.push_back(vertex);
        }
    }

    // Generate indices
    for (int z = 0; z < subdivisions; z++) {
        for (int x = 0; x < subdivisions; x++) {
            uint32_t topLeft = z * verticesPerRow + x;
            uint32_t topRight = topLeft + 1;
            uint32_t bottomLeft = (z + 1) * verticesPerRow + x;
            uint32_t bottomRight = bottomLeft + 1;

            indices.push_back(topLeft);
            indices.push_back(bottomLeft);
            indices.push_back(topRight);

            indices.push_back(topRight);
            indices.push_back(bottomLeft);
            indices.push_back(bottomRight);
        }
    }

    Mesh mesh("plane", vertices, indices);
    mesh.calculateBounds();
    return mesh;
}
