/**
 * @file mesh_loader.h
 * @brief Mesh loading from various file formats (OBJ, glTF)
 */

#pragma once

#include "mesh/mesh.h"
#include <vector>
#include <string>
#include <memory>

/**
 * @brief Static mesh loading utilities
 *
 * Supports loading meshes from various formats:
 * - OBJ (Wavefront) - Simple, widely supported
 * - glTF 2.0 - Modern, efficient, PBR materials (future)
 *
 * Also provides procedural mesh generation for primitives.
 */
class MeshLoader {
public:
    /**
     * @brief Load mesh from OBJ file (basic, no materials)
     *
     * Loads vertex positions, normals, and UVs from a Wavefront OBJ file.
     * Does NOT load materials in Phase 1 (returns default material).
     * Automatically calculates tangents and bounding box.
     *
     * Supports:
     * - Vertex positions (v)
     * - Texture coordinates (vt)
     * - Vertex normals (vn)
     * - Face definitions (f)
     *
     * @param filepath Path to .obj file
     * @param materials Output vector of materials (will contain 1 default material)
     * @return Vector of loaded meshes (usually 1 mesh per OBJ file)
     * @throws std::runtime_error if file not found or parsing fails
     */
    static std::vector<Mesh> loadOBJ(const std::string& filepath,
                                      std::vector<PBRMaterial>& materials);

    /**
     * @brief Load mesh from glTF 2.0 file (future implementation)
     *
     * @param filepath Path to .gltf or .glb file
     * @param materials Output vector of PBR materials
     * @return Vector of loaded meshes
     * @throws std::runtime_error if not implemented or file invalid
     */
    static std::vector<Mesh> loadGLTF(const std::string& filepath,
                                       std::vector<PBRMaterial>& materials);

    // ========== Procedural Mesh Generators ==========

    /**
     * @brief Create cube mesh
     * @param size Edge length of cube
     * @return Cube mesh with normals and UVs
     */
    static Mesh createCube(float size = 1.0f);

    /**
     * @brief Create UV sphere mesh
     * @param radius Sphere radius
     * @param segments Number of horizontal and vertical segments
     * @return Sphere mesh with smooth normals
     */
    static Mesh createSphere(float radius = 1.0f, int segments = 32);

    /**
     * @brief Create cylinder mesh
     * @param radius Cylinder radius
     * @param height Cylinder height
     * @param segments Number of radial segments
     * @return Cylinder mesh with caps
     */
    static Mesh createCylinder(float radius = 1.0f, float height = 2.0f, int segments = 32);

    /**
     * @brief Create plane mesh (useful for testing)
     * @param width Plane width
     * @param depth Plane depth
     * @param subdivisions Number of subdivisions per axis
     * @return Flat plane mesh
     */
    static Mesh createPlane(float width = 1.0f, float depth = 1.0f, int subdivisions = 1);

private:
    /**
     * @brief Parse OBJ face definition (supports f v, f v/vt, f v/vt/vn)
     * @param faceStr Face string from OBJ file
     * @param vertexIdx Output vertex index
     * @param uvIdx Output UV index
     * @param normalIdx Output normal index
     * @return True if parsed successfully
     */
    static bool parseOBJFace(const std::string& faceStr,
                             int& vertexIdx, int& uvIdx, int& normalIdx);

    /**
     * @brief Convert OBJ-style negative indices to positive
     * @param index Input index (negative or positive)
     * @param size Array size
     * @return Positive index
     */
    static int convertOBJIndex(int index, size_t size);
};
