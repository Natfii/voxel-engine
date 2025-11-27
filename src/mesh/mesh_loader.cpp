/**
 * @file mesh_loader.cpp
 * @brief Implementation of mesh loading from various formats
 */

#include "mesh/mesh_loader.h"
#include "logger.h"
#define GLM_ENABLE_EXPERIMENTAL  // Required for gtx extensions
#include <glm/gtc/constants.hpp>  // For glm::pi
#include <glm/gtc/quaternion.hpp>  // For glm::quat
#include <glm/gtx/quaternion.hpp>  // For glm::mat4_cast
#include <glm/gtc/matrix_transform.hpp>  // For glm::translate, glm::rotate, glm::scale
#include <fstream>
#include <sstream>
#include <cmath>
#include <cfloat>
#include <functional>
#include <unordered_map>

// tinygltf for GLB/GLTF loading
// STB_IMAGE_IMPLEMENTATION is already defined in block_system.cpp
// Include stb_image.h first so tinygltf can use the external implementation
#include "stb_image.h"
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE  // Don't need image writing
#define STBI_INCLUDE_STB_IMAGE_H     // Tell tinygltf we already included stb_image.h
#include "tiny_gltf.h"

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

// ========== glTF/GLB Loader ==========

std::vector<Mesh> MeshLoader::loadGLTF(const std::string& filepath,
                                        std::vector<PBRMaterial>& materials,
                                        std::vector<TextureImage>& textures) {
    Logger::info() << "Loading glTF/GLB mesh: " << filepath;

    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    // Determine if it's a binary GLB or text GLTF
    bool isBinary = filepath.size() >= 4 &&
                    (filepath.substr(filepath.size() - 4) == ".glb" ||
                     filepath.substr(filepath.size() - 4) == ".GLB");

    bool success = false;
    if (isBinary) {
        success = loader.LoadBinaryFromFile(&model, &err, &warn, filepath);
    } else {
        success = loader.LoadASCIIFromFile(&model, &err, &warn, filepath);
    }

    if (!warn.empty()) {
        Logger::warning() << "glTF warning: " << warn;
    }

    if (!err.empty()) {
        throw std::runtime_error("glTF error: " + err);
    }

    if (!success) {
        throw std::runtime_error("Failed to load glTF file: " + filepath);
    }

    std::vector<Mesh> meshes;

    // ========== Extract Textures ==========
    textures.clear();

    // Map from glTF texture index to our texture array index
    std::unordered_map<int, int32_t> textureIndexMap;

    for (size_t i = 0; i < model.textures.size(); ++i) {
        const tinygltf::Texture& gltfTex = model.textures[i];

        // Get the image source
        if (gltfTex.source < 0 || gltfTex.source >= static_cast<int>(model.images.size())) {
            Logger::warning() << "Texture " << i << " has invalid image source";
            continue;
        }

        const tinygltf::Image& gltfImage = model.images[gltfTex.source];

        TextureImage texImage;
        texImage.name = gltfImage.name.empty() ? "texture_" + std::to_string(i) : gltfImage.name;

        // Check if image data is already decoded (tinygltf does this for us)
        if (!gltfImage.image.empty()) {
            // tinygltf already decoded the image
            texImage.width = static_cast<uint32_t>(gltfImage.width);
            texImage.height = static_cast<uint32_t>(gltfImage.height);

            // Convert to RGBA if necessary
            if (gltfImage.component == 4) {
                // Already RGBA
                texImage.data = gltfImage.image;
            } else if (gltfImage.component == 3) {
                // RGB -> RGBA
                texImage.data.resize(texImage.width * texImage.height * 4);
                for (size_t p = 0; p < texImage.width * texImage.height; ++p) {
                    texImage.data[p * 4 + 0] = gltfImage.image[p * 3 + 0];
                    texImage.data[p * 4 + 1] = gltfImage.image[p * 3 + 1];
                    texImage.data[p * 4 + 2] = gltfImage.image[p * 3 + 2];
                    texImage.data[p * 4 + 3] = 255;
                }
            } else if (gltfImage.component == 1) {
                // Grayscale -> RGBA
                texImage.data.resize(texImage.width * texImage.height * 4);
                for (size_t p = 0; p < texImage.width * texImage.height; ++p) {
                    uint8_t gray = gltfImage.image[p];
                    texImage.data[p * 4 + 0] = gray;
                    texImage.data[p * 4 + 1] = gray;
                    texImage.data[p * 4 + 2] = gray;
                    texImage.data[p * 4 + 3] = 255;
                }
            } else if (gltfImage.component == 2) {
                // Grayscale+Alpha -> RGBA
                texImage.data.resize(texImage.width * texImage.height * 4);
                for (size_t p = 0; p < texImage.width * texImage.height; ++p) {
                    uint8_t gray = gltfImage.image[p * 2 + 0];
                    uint8_t alpha = gltfImage.image[p * 2 + 1];
                    texImage.data[p * 4 + 0] = gray;
                    texImage.data[p * 4 + 1] = gray;
                    texImage.data[p * 4 + 2] = gray;
                    texImage.data[p * 4 + 3] = alpha;
                }
            }

            if (texImage.isValid()) {
                textureIndexMap[static_cast<int>(i)] = static_cast<int32_t>(textures.size());
                textures.push_back(std::move(texImage));
                Logger::info() << "Loaded texture " << i << ": " << textures.back().name
                              << " (" << textures.back().width << "x" << textures.back().height << ")";
            } else {
                Logger::warning() << "Failed to process texture " << i;
            }
        } else {
            Logger::warning() << "Texture " << i << " has no decoded image data";
        }
    }

    Logger::info() << "Extracted " << textures.size() << " textures from glTF";

    // ========== Load Materials ==========
    materials.clear();
    for (size_t matIdx = 0; matIdx < model.materials.size(); ++matIdx) {
        const auto& gltfMat = model.materials[matIdx];
        PBRMaterial mat = PBRMaterial::createDefault();

        // Base color
        if (gltfMat.pbrMetallicRoughness.baseColorFactor.size() >= 4) {
            mat.baseColor = glm::vec4(
                static_cast<float>(gltfMat.pbrMetallicRoughness.baseColorFactor[0]),
                static_cast<float>(gltfMat.pbrMetallicRoughness.baseColorFactor[1]),
                static_cast<float>(gltfMat.pbrMetallicRoughness.baseColorFactor[2]),
                static_cast<float>(gltfMat.pbrMetallicRoughness.baseColorFactor[3])
            );
        }

        // Metallic and roughness
        mat.metallic = static_cast<float>(gltfMat.pbrMetallicRoughness.metallicFactor);
        mat.roughness = static_cast<float>(gltfMat.pbrMetallicRoughness.roughnessFactor);

        // Emissive
        if (gltfMat.emissiveFactor.size() >= 3) {
            float emissive = static_cast<float>(
                (gltfMat.emissiveFactor[0] + gltfMat.emissiveFactor[1] + gltfMat.emissiveFactor[2]) / 3.0
            );
            mat.emissive = emissive;
        }

        // Base color texture (albedo)
        int baseColorTexIdx = gltfMat.pbrMetallicRoughness.baseColorTexture.index;
        if (baseColorTexIdx >= 0) {
            auto it = textureIndexMap.find(baseColorTexIdx);
            if (it != textureIndexMap.end()) {
                mat.albedoTexture = it->second;
            }
        }

        // Normal texture
        int normalTexIdx = gltfMat.normalTexture.index;
        if (normalTexIdx >= 0) {
            auto it = textureIndexMap.find(normalTexIdx);
            if (it != textureIndexMap.end()) {
                mat.normalTexture = it->second;
            }
        }

        // Metallic-roughness texture
        int mrTexIdx = gltfMat.pbrMetallicRoughness.metallicRoughnessTexture.index;
        if (mrTexIdx >= 0) {
            auto it = textureIndexMap.find(mrTexIdx);
            if (it != textureIndexMap.end()) {
                mat.metallicRoughnessTexture = it->second;
            }
        }

        // Emissive texture
        int emissiveTexIdx = gltfMat.emissiveTexture.index;
        if (emissiveTexIdx >= 0) {
            auto it = textureIndexMap.find(emissiveTexIdx);
            if (it != textureIndexMap.end()) {
                mat.emissiveTexture = it->second;
            }
        }

        materials.push_back(mat);
        Logger::info() << "Material " << matIdx << ": albedoTex=" << mat.albedoTexture
                      << ", normalTex=" << mat.normalTexture
                      << ", mrTex=" << mat.metallicRoughnessTexture;
    }

    // If no materials, add default
    if (materials.empty()) {
        materials.push_back(PBRMaterial::createDefault());
    }

    // Process each mesh in the model
    for (const auto& gltfMesh : model.meshes) {
        for (const auto& primitive : gltfMesh.primitives) {
            if (primitive.mode != TINYGLTF_MODE_TRIANGLES) {
                Logger::warning() << "Skipping non-triangle primitive in mesh: " << gltfMesh.name;
                continue;
            }

            std::vector<MeshVertex> vertices;
            std::vector<uint32_t> indices;

            // Get accessors
            const tinygltf::Accessor* posAccessor = nullptr;
            const tinygltf::Accessor* normalAccessor = nullptr;
            const tinygltf::Accessor* uvAccessor = nullptr;
            const tinygltf::Accessor* colorAccessor = nullptr;

            // Position (required)
            auto posIt = primitive.attributes.find("POSITION");
            if (posIt == primitive.attributes.end()) {
                Logger::warning() << "Mesh primitive has no POSITION attribute, skipping";
                continue;
            }
            posAccessor = &model.accessors[posIt->second];

            // Normal (optional)
            auto normIt = primitive.attributes.find("NORMAL");
            if (normIt != primitive.attributes.end()) {
                normalAccessor = &model.accessors[normIt->second];
            }

            // UV (optional)
            auto uvIt = primitive.attributes.find("TEXCOORD_0");
            if (uvIt != primitive.attributes.end()) {
                uvAccessor = &model.accessors[uvIt->second];
            }

            // Vertex color (optional - used by PS1-style models)
            auto colorIt = primitive.attributes.find("COLOR_0");
            if (colorIt != primitive.attributes.end()) {
                colorAccessor = &model.accessors[colorIt->second];
            }

            // Get buffer views with validation
            if (posAccessor->bufferView < 0 || posAccessor->bufferView >= static_cast<int>(model.bufferViews.size())) {
                Logger::warning() << "Invalid buffer view index for POSITION, skipping primitive";
                continue;
            }
            const tinygltf::BufferView& posView = model.bufferViews[posAccessor->bufferView];

            if (posView.buffer < 0 || posView.buffer >= static_cast<int>(model.buffers.size())) {
                Logger::warning() << "Invalid buffer index for POSITION, skipping primitive";
                continue;
            }
            const tinygltf::Buffer& posBuffer = model.buffers[posView.buffer];

            // Read vertex data
            size_t vertexCount = posAccessor->count;
            vertices.resize(vertexCount);

            // Validate buffer size
            size_t requiredPosBytes = posView.byteOffset + posAccessor->byteOffset + vertexCount * 12;
            if (requiredPosBytes > posBuffer.data.size()) {
                Logger::warning() << "Position buffer too small, skipping primitive";
                continue;
            }

            for (size_t i = 0; i < vertexCount; ++i) {
                MeshVertex& vertex = vertices[i];

                // Position
                const float* posData = reinterpret_cast<const float*>(
                    &posBuffer.data[posView.byteOffset + posAccessor->byteOffset + i * 12]
                );
                vertex.position = glm::vec3(posData[0], posData[1], posData[2]);

                // Normal
                if (normalAccessor) {
                    const tinygltf::BufferView& normView = model.bufferViews[normalAccessor->bufferView];
                    const tinygltf::Buffer& normBuffer = model.buffers[normView.buffer];
                    const float* normData = reinterpret_cast<const float*>(
                        &normBuffer.data[normView.byteOffset + normalAccessor->byteOffset + i * 12]
                    );
                    vertex.normal = glm::normalize(glm::vec3(normData[0], normData[1], normData[2]));
                } else {
                    vertex.normal = glm::vec3(0, 1, 0);
                }

                // UV
                if (uvAccessor) {
                    const tinygltf::BufferView& uvView = model.bufferViews[uvAccessor->bufferView];
                    const tinygltf::Buffer& uvBuffer = model.buffers[uvView.buffer];
                    const float* uvData = reinterpret_cast<const float*>(
                        &uvBuffer.data[uvView.byteOffset + uvAccessor->byteOffset + i * 8]
                    );
                    vertex.texCoord = glm::vec2(uvData[0], uvData[1]);
                } else {
                    vertex.texCoord = glm::vec2(0, 0);
                }

                // Vertex color (for PS1-style models)
                if (colorAccessor) {
                    const tinygltf::BufferView& colorView = model.bufferViews[colorAccessor->bufferView];
                    const tinygltf::Buffer& colorBuffer = model.buffers[colorView.buffer];

                    // COLOR_0 can be vec3 or vec4, and float or normalized unsigned byte/short
                    int numComponents = (colorAccessor->type == TINYGLTF_TYPE_VEC4) ? 4 : 3;

                    if (colorAccessor->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
                        // Float colors
                        size_t stride = numComponents * sizeof(float);
                        const float* colorData = reinterpret_cast<const float*>(
                            &colorBuffer.data[colorView.byteOffset + colorAccessor->byteOffset + i * stride]
                        );
                        vertex.color.r = colorData[0];
                        vertex.color.g = colorData[1];
                        vertex.color.b = colorData[2];
                        vertex.color.a = (numComponents == 4) ? colorData[3] : 1.0f;
                    } else if (colorAccessor->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                        // Normalized unsigned byte colors (0-255 -> 0.0-1.0)
                        size_t stride = numComponents * sizeof(uint8_t);
                        const uint8_t* colorData =
                            &colorBuffer.data[colorView.byteOffset + colorAccessor->byteOffset + i * stride];
                        vertex.color.r = colorData[0] / 255.0f;
                        vertex.color.g = colorData[1] / 255.0f;
                        vertex.color.b = colorData[2] / 255.0f;
                        vertex.color.a = (numComponents == 4) ? colorData[3] / 255.0f : 1.0f;
                    } else if (colorAccessor->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                        // Normalized unsigned short colors (0-65535 -> 0.0-1.0)
                        size_t stride = numComponents * sizeof(uint16_t);
                        const uint16_t* colorData = reinterpret_cast<const uint16_t*>(
                            &colorBuffer.data[colorView.byteOffset + colorAccessor->byteOffset + i * stride]
                        );
                        vertex.color.r = colorData[0] / 65535.0f;
                        vertex.color.g = colorData[1] / 65535.0f;
                        vertex.color.b = colorData[2] / 65535.0f;
                        vertex.color.a = (numComponents == 4) ? colorData[3] / 65535.0f : 1.0f;
                    }
                }
                // Note: vertex.color defaults to white (1,1,1,1) via MeshVertex constructor

                // Default tangent
                vertex.tangent = glm::vec3(1, 0, 0);
            }

            // Read indices
            if (primitive.indices >= 0) {
                const tinygltf::Accessor& idxAccessor = model.accessors[primitive.indices];
                const tinygltf::BufferView& idxView = model.bufferViews[idxAccessor.bufferView];
                const tinygltf::Buffer& idxBuffer = model.buffers[idxView.buffer];

                indices.reserve(idxAccessor.count);

                const uint8_t* dataPtr = &idxBuffer.data[idxView.byteOffset + idxAccessor.byteOffset];

                for (size_t i = 0; i < idxAccessor.count; ++i) {
                    uint32_t index = 0;
                    switch (idxAccessor.componentType) {
                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                            index = dataPtr[i];
                            break;
                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                            index = reinterpret_cast<const uint16_t*>(dataPtr)[i];
                            break;
                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                            index = reinterpret_cast<const uint32_t*>(dataPtr)[i];
                            break;
                        default:
                            Logger::warning() << "Unknown index component type: " << idxAccessor.componentType;
                            index = 0;
                    }
                    indices.push_back(index);
                }
            } else {
                // No indices, generate sequential
                for (size_t i = 0; i < vertexCount; ++i) {
                    indices.push_back(static_cast<uint32_t>(i));
                }
            }

            // Create mesh
            std::string meshName = gltfMesh.name.empty() ? "mesh_" + std::to_string(meshes.size()) : gltfMesh.name;
            Mesh mesh(meshName, vertices, indices);
            mesh.calculateTangents();
            mesh.calculateBounds();

            // Set material index
            if (primitive.material >= 0) {
                mesh.materialIndex = static_cast<uint32_t>(primitive.material);
            }

            Logger::info() << "  Mesh '" << meshName << "': " << vertices.size() << " verts, "
                          << indices.size() / 3 << " tris"
                          << (colorAccessor ? " (with vertex colors)" : "");

            meshes.push_back(std::move(mesh));
        }
    }

    if (meshes.empty()) {
        throw std::runtime_error("glTF file contains no valid meshes: " + filepath);
    }

    Logger::info() << "Loaded glTF: " << meshes.size() << " meshes, " << materials.size() << " materials";
    return meshes;
}

// ========== glTF Scene Loader with Hierarchy ==========

GLTFScene MeshLoader::loadGLTFScene(const std::string& filepath) {
    Logger::info() << "Loading glTF scene with hierarchy: " << filepath;

    GLTFScene scene;

    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    // Determine if it's a binary GLB or text GLTF
    bool isBinary = filepath.size() >= 4 &&
                    (filepath.substr(filepath.size() - 4) == ".glb" ||
                     filepath.substr(filepath.size() - 4) == ".GLB");

    bool success = false;
    if (isBinary) {
        success = loader.LoadBinaryFromFile(&model, &err, &warn, filepath);
    } else {
        success = loader.LoadASCIIFromFile(&model, &err, &warn, filepath);
    }

    if (!warn.empty()) {
        Logger::warning() << "glTF warning: " << warn;
    }

    if (!err.empty()) {
        throw std::runtime_error("glTF error: " + err);
    }

    if (!success) {
        throw std::runtime_error("Failed to load glTF file: " + filepath);
    }

    // Load meshes, materials, and textures using existing code
    scene.meshes = loadGLTF(filepath, scene.materials, scene.textures);

    // ========== Extract Node Hierarchy ==========
    scene.nodes.resize(model.nodes.size());

    // First pass: Extract node data and local transforms
    for (size_t i = 0; i < model.nodes.size(); ++i) {
        const tinygltf::Node& gltfNode = model.nodes[i];
        GLTFNode& node = scene.nodes[i];

        // Set node name
        node.name = gltfNode.name.empty() ? "node_" + std::to_string(i) : gltfNode.name;

        // Set mesh index if present
        node.meshIndex = gltfNode.mesh;

        // Store children indices
        node.children = gltfNode.children;

        // Compute local transform matrix
        if (!gltfNode.matrix.empty() && gltfNode.matrix.size() >= 16) {
            // Node has a direct matrix
            // glTF uses column-major order (same as glm)
            // matrix[col * 4 + row] = element at (row, col)
            for (int col = 0; col < 4; ++col) {
                for (int row = 0; row < 4; ++row) {
                    node.localTransform[col][row] = static_cast<float>(gltfNode.matrix[col * 4 + row]);
                }
            }
        } else {
            // Compose from TRS (Translation, Rotation, Scale)
            glm::mat4 translation = glm::mat4(1.0f);
            glm::mat4 rotation = glm::mat4(1.0f);
            glm::mat4 scale = glm::mat4(1.0f);

            // Translation
            if (!gltfNode.translation.empty()) {
                translation = glm::translate(glm::mat4(1.0f),
                    glm::vec3(
                        static_cast<float>(gltfNode.translation[0]),
                        static_cast<float>(gltfNode.translation[1]),
                        static_cast<float>(gltfNode.translation[2])
                    ));
            }

            // Rotation (quaternion)
            if (!gltfNode.rotation.empty()) {
                glm::quat quat(
                    static_cast<float>(gltfNode.rotation[3]), // w
                    static_cast<float>(gltfNode.rotation[0]), // x
                    static_cast<float>(gltfNode.rotation[1]), // y
                    static_cast<float>(gltfNode.rotation[2])  // z
                );
                rotation = glm::mat4_cast(quat);
            }

            // Scale
            if (!gltfNode.scale.empty()) {
                scale = glm::scale(glm::mat4(1.0f),
                    glm::vec3(
                        static_cast<float>(gltfNode.scale[0]),
                        static_cast<float>(gltfNode.scale[1]),
                        static_cast<float>(gltfNode.scale[2])
                    ));
            }

            // Compose: T * R * S
            node.localTransform = translation * rotation * scale;
        }
    }

    // Second pass: Set parent indices
    for (size_t i = 0; i < scene.nodes.size(); ++i) {
        for (int childIdx : scene.nodes[i].children) {
            if (childIdx >= 0 && childIdx < static_cast<int>(scene.nodes.size())) {
                scene.nodes[childIdx].parent = static_cast<int>(i);
            }
        }
    }

    // ========== Calculate World Transforms and Scene Bounds ==========
    // First, compute world transforms for all nodes
    std::vector<glm::mat4> worldTransforms(scene.nodes.size(), glm::mat4(1.0f));

    // Helper lambda to compute world transform recursively
    std::function<glm::mat4(int, const glm::mat4&)> computeWorldTransform;
    computeWorldTransform = [&](int nodeIdx, const glm::mat4& parentWorld) -> glm::mat4 {
        if (nodeIdx < 0 || nodeIdx >= static_cast<int>(scene.nodes.size())) {
            return parentWorld;
        }
        glm::mat4 world = parentWorld * scene.nodes[nodeIdx].localTransform;
        worldTransforms[nodeIdx] = world;

        for (int childIdx : scene.nodes[nodeIdx].children) {
            computeWorldTransform(childIdx, world);
        }
        return world;
    };

    // Start from root nodes (nodes with no parent)
    for (size_t i = 0; i < scene.nodes.size(); ++i) {
        if (scene.nodes[i].parent < 0) {
            computeWorldTransform(static_cast<int>(i), glm::mat4(1.0f));
        }
    }

    // Now compute scene bounds by transforming mesh bounds by their node's world transform
    scene.boundsMin = glm::vec3(FLT_MAX);
    scene.boundsMax = glm::vec3(-FLT_MAX);

    for (size_t nodeIdx = 0; nodeIdx < scene.nodes.size(); ++nodeIdx) {
        const GLTFNode& node = scene.nodes[nodeIdx];
        if (node.meshIndex >= 0 && node.meshIndex < static_cast<int>(scene.meshes.size())) {
            const Mesh& mesh = scene.meshes[node.meshIndex];
            const glm::mat4& worldTransform = worldTransforms[nodeIdx];

            // Transform the 8 corners of the mesh bounding box
            glm::vec3 corners[8] = {
                glm::vec3(mesh.boundsMin.x, mesh.boundsMin.y, mesh.boundsMin.z),
                glm::vec3(mesh.boundsMax.x, mesh.boundsMin.y, mesh.boundsMin.z),
                glm::vec3(mesh.boundsMin.x, mesh.boundsMax.y, mesh.boundsMin.z),
                glm::vec3(mesh.boundsMax.x, mesh.boundsMax.y, mesh.boundsMin.z),
                glm::vec3(mesh.boundsMin.x, mesh.boundsMin.y, mesh.boundsMax.z),
                glm::vec3(mesh.boundsMax.x, mesh.boundsMin.y, mesh.boundsMax.z),
                glm::vec3(mesh.boundsMin.x, mesh.boundsMax.y, mesh.boundsMax.z),
                glm::vec3(mesh.boundsMax.x, mesh.boundsMax.y, mesh.boundsMax.z)
            };

            for (int c = 0; c < 8; ++c) {
                glm::vec4 transformed = worldTransform * glm::vec4(corners[c], 1.0f);
                glm::vec3 worldPos = glm::vec3(transformed) / transformed.w;
                scene.boundsMin = glm::min(scene.boundsMin, worldPos);
                scene.boundsMax = glm::max(scene.boundsMax, worldPos);
            }
        }
    }

    // If no meshes had nodes, fall back to direct mesh bounds
    if (scene.boundsMin.x == FLT_MAX) {
        for (const Mesh& mesh : scene.meshes) {
            scene.boundsMin = glm::min(scene.boundsMin, mesh.boundsMin);
            scene.boundsMax = glm::max(scene.boundsMax, mesh.boundsMax);
        }
    }

    Logger::info() << "Loaded glTF scene: " << scene.nodes.size() << " nodes, "
                  << scene.meshes.size() << " meshes, "
                  << scene.materials.size() << " materials, "
                  << scene.textures.size() << " textures";

    return scene;
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
