/**
 * @file frustum.h
 * @brief View frustum culling for efficient chunk rendering
 *
 */

#pragma once

#include <glm/glm.hpp>
#include <cmath>

/**
 * @brief Represents a plane in 3D space using implicit form
 *
 * Plane Equation: Ax + By + Cz + D = 0
 * =====================================
 *
 * A plane divides 3D space into two half-spaces:
 * - Points with (Ax + By + Cz + D) > 0 are in front of the plane
 * - Points with (Ax + By + Cz + D) < 0 are behind the plane
 * - Points with (Ax + By + Cz + D) = 0 are exactly on the plane
 *
 * Normal Vector:
 * --------------
 * The coefficients (a, b, c) form the plane's normal vector n = (a, b, c).
 * This vector points in the direction of "positive" space (front of plane).
 *
 * Normalization:
 * --------------
 * Normalizing the plane ensures that:
 * - The normal vector (a, b, c) has unit length: √(a² + b² + c²) = 1
 * - The distance calculation returns actual Euclidean distance
 * - Without normalization, distance is scaled by normal magnitude
 *
 * Distance Calculation:
 * ---------------------
 * For a normalized plane, distanceToPoint(p) returns the signed distance:
 * - Positive: point is in front of plane (in direction of normal)
 * - Negative: point is behind plane
 * - Zero: point is on plane
 *
 * Example:
 * --------
 * Plane with equation: 0x + 1y + 0z - 5 = 0  (horizontal plane at y=5)
 * - Normal vector: (0, 1, 0) pointing upward
 * - Point (3, 8, 2): distance = 0*3 + 1*8 + 0*2 - 5 = 3 (above plane)
 * - Point (3, 2, 2): distance = 0*3 + 1*2 + 0*2 - 5 = -3 (below plane)
 */
struct Plane {
    float a, b, c, d;  ///< Plane coefficients: Ax + By + Cz + D = 0

    /**
     * @brief Normalizes the plane equation to unit normal length
     *
     * Divides all coefficients (a, b, c, d) by the magnitude of the normal
     * vector √(a² + b² + c²). After normalization, distanceToPoint() returns
     * true Euclidean distance instead of scaled distance.
     */
    void normalize() {
        float mag = std::sqrt(a * a + b * b + c * c);
        if (mag > 0.0f) {
            a /= mag;
            b /= mag;
            c /= mag;
            d /= mag;
        }
    }

    /**
     * @brief Calculates signed distance from point to plane
     *
     * For a normalized plane, this returns the perpendicular distance from
     * the point to the plane. Positive values mean the point is in front
     * (in the direction of the plane's normal), negative means behind.
     *
     * @param point Point in 3D space to test
     * @return Signed distance (positive = front, negative = behind, 0 = on plane)
     */
    float distanceToPoint(const glm::vec3& point) const {
        return a * point.x + b * point.y + c * point.z + d;
    }
};

/**
 * @brief View frustum represented by 6 bounding planes
 *
 * Frustum Culling Overview:
 * ==========================
 *
 * The view frustum is a truncated pyramid (frustum) representing the visible
 * region of 3D space from the camera's perspective. Objects outside this region
 * are culled (not rendered) to improve performance.
 *
 * The 6 Planes:
 * -------------
 * 1. LEFT plane:   Right boundary of visible space (left side of screen)
 * 2. RIGHT plane:  Left boundary of visible space (right side of screen)
 * 3. BOTTOM plane: Top boundary of visible space (bottom of screen)
 * 4. TOP plane:    Bottom boundary of visible space (top of screen)
 * 5. NEAR plane:   Closest visible distance (camera near clip)
 * 6. FAR plane:    Farthest visible distance (camera far clip)
 *
 * Note: The plane normals point INWARD toward visible space.
 *
 * Visual Representation:
 * ----------------------
 * ```
 *           TOP
 *            /\
 *           /  \
 *   LEFT   /    \   RIGHT        (View from above)
 *         /camera\
 *        /________\
 *       NEAR  |  FAR
 *           BOTTOM
 * ```
 *
 * Culling Test:
 * -------------
 * An object is visible if it's inside ALL 6 planes (intersection of half-spaces).
 * If the object is outside ANY plane, it's completely culled.
 *
 * Performance Impact:
 * -------------------
 * In a typical voxel world with 20x20 chunk grid (400 chunks):
 * - Without culling: Render all 400 chunks
 * - With frustum culling: Render ~60-100 chunks (85% reduction)
 * - Additional distance culling: Render ~20-40 chunks (95% reduction)
 */
struct Frustum {
    Plane planes[6];  ///< The 6 bounding planes of the frustum

    enum { LEFT = 0, RIGHT, BOTTOM, TOP, NEAR, FAR };  ///< Plane indices
};

/**
 * @brief Extracts frustum planes from view-projection matrix
 *
 * Gribb-Hartmann Method:
 * =======================
 *
 * This function uses the Gribb-Hartmann plane extraction method to derive
 * the 6 frustum planes directly from the combined view-projection matrix.
 *
 * Mathematical Background:
 * ------------------------
 * The view-projection matrix M transforms world-space coordinates to NDC
 * (Normalized Device Coordinates). After transformation and perspective divide:
 *
 *   [x']   [M00 M01 M02 M03]   [x]       [x'/w']
 *   [y'] = [M10 M11 M12 M13] * [y]  =>   [y'/w']  in NDC
 *   [z']   [M20 M21 M22 M23]   [z]       [z'/w']
 *   [w']   [M30 M31 M32 M33]   [w=1]
 *
 * In NDC space:
 * - Visible X range: -1 ≤ x'/w' ≤ 1
 * - Visible Y range: -1 ≤ y'/w' ≤ 1  (Vulkan: 0 ≤ y'/w' ≤ 1 after flip)
 * - Visible Z range:  0 ≤ z'/w' ≤ 1  (Vulkan depth range)
 *
 * Plane Extraction Formula:
 * -------------------------
 * Each frustum boundary corresponds to an NDC limit. For example:
 *
 * - Left plane (x'/w' = -1):  M[row3] + M[row0] = 0  =>  plane equation
 * - Right plane (x'/w' = 1):  M[row3] - M[row0] = 0  =>  plane equation
 * - Bottom plane (y'/w' = -1): M[row3] + M[row1] = 0
 * - Top plane (y'/w' = 1):     M[row3] - M[row1] = 0
 * - Near plane (z'/w' = 0):    M[row2] = 0
 * - Far plane (z'/w' = 1):     M[row3] - M[row2] = 0
 *
 * Vulkan Coordinate System Notes:
 * --------------------------------
 * Vulkan uses different conventions than OpenGL:
 * - Y-axis points DOWN in NDC (glm handles this with GLM_FORCE_DEPTH_ZERO_TO_ONE)
 * - Z-axis range is [0, 1] instead of [-1, 1]
 * - Projection matrix includes Y-flip: projection[1][1] *= -1
 *
 * The plane extraction already accounts for Vulkan's Y-flip because it operates
 * on the combined view-projection matrix after the flip has been applied.
 *
 * Why Normalize Planes?
 * ---------------------
 * Normalization ensures that distanceToPoint() returns true Euclidean distance,
 * making the margin parameter in intersection tests meaningful (in world units).
 *
 * Algorithm Steps:
 * ----------------
 * 1. Extract 6 planes by combining matrix rows according to NDC boundaries
 * 2. Normalize each plane to ensure unit-length normals
 * 3. Return frustum with inward-pointing plane normals
 *
 * Usage Example:
 * --------------
 * ```cpp
 * glm::mat4 view = camera.getViewMatrix();
 * glm::mat4 proj = camera.getProjectionMatrix();
 * glm::mat4 viewProj = proj * view;
 * Frustum frustum = extractFrustum(viewProj);
 *
 * // Test if chunk is visible
 * if (frustumAABBIntersect(frustum, chunk.getMin(), chunk.getMax())) {
 *     chunk.render(commandBuffer);
 * }
 * ```
 *
 * References:
 * -----------
 * - "Fast Extraction of Viewing Frustum Planes from the World-View-Projection Matrix"
 *   by Gil Gribb and Klaus Hartmann (2001)
 * - Vulkan coordinate system: https://matthewwellings.com/blog/the-new-vulkan-coordinate-system/
 *
 * @param viewProj Combined view-projection matrix (projection * view)
 * @return Frustum with 6 normalized planes
 */
inline Frustum extractFrustum(const glm::mat4& viewProj) {
    Frustum frustum;

    // Gribb-Hartmann method: Extract planes by combining matrix rows
    // For Vulkan with Y-flip already applied in projection matrix

    // Left plane: column4 + column1
    frustum.planes[Frustum::LEFT].a = viewProj[0][3] + viewProj[0][0];
    frustum.planes[Frustum::LEFT].b = viewProj[1][3] + viewProj[1][0];
    frustum.planes[Frustum::LEFT].c = viewProj[2][3] + viewProj[2][0];
    frustum.planes[Frustum::LEFT].d = viewProj[3][3] + viewProj[3][0];

    // Right plane: column4 - column1
    frustum.planes[Frustum::RIGHT].a = viewProj[0][3] - viewProj[0][0];
    frustum.planes[Frustum::RIGHT].b = viewProj[1][3] - viewProj[1][0];
    frustum.planes[Frustum::RIGHT].c = viewProj[2][3] - viewProj[2][0];
    frustum.planes[Frustum::RIGHT].d = viewProj[3][3] - viewProj[3][0];

    // Bottom plane: column4 + column2
    frustum.planes[Frustum::BOTTOM].a = viewProj[0][3] + viewProj[0][1];
    frustum.planes[Frustum::BOTTOM].b = viewProj[1][3] + viewProj[1][1];
    frustum.planes[Frustum::BOTTOM].c = viewProj[2][3] + viewProj[2][1];
    frustum.planes[Frustum::BOTTOM].d = viewProj[3][3] + viewProj[3][1];

    // Top plane: column4 - column2
    frustum.planes[Frustum::TOP].a = viewProj[0][3] - viewProj[0][1];
    frustum.planes[Frustum::TOP].b = viewProj[1][3] - viewProj[1][1];
    frustum.planes[Frustum::TOP].c = viewProj[2][3] - viewProj[2][1];
    frustum.planes[Frustum::TOP].d = viewProj[3][3] - viewProj[3][1];

    // Near plane: For Vulkan [0,1] depth, near plane is column3
    frustum.planes[Frustum::NEAR].a = viewProj[0][2];
    frustum.planes[Frustum::NEAR].b = viewProj[1][2];
    frustum.planes[Frustum::NEAR].c = viewProj[2][2];
    frustum.planes[Frustum::NEAR].d = viewProj[3][2];

    // Far plane: column4 - column3
    frustum.planes[Frustum::FAR].a = viewProj[0][3] - viewProj[0][2];
    frustum.planes[Frustum::FAR].b = viewProj[1][3] - viewProj[1][2];
    frustum.planes[Frustum::FAR].c = viewProj[2][3] - viewProj[2][2];
    frustum.planes[Frustum::FAR].d = viewProj[3][3] - viewProj[3][2];

    // Normalize all planes
    for (int i = 0; i < 6; ++i) {
        frustum.planes[i].normalize();
    }

    return frustum;
}

/**
 * @brief Tests if an AABB intersects with the view frustum
 *
 * AABB-Frustum Intersection Algorithm:
 * =====================================
 *
 * This function performs frustum culling by testing if an Axis-Aligned Bounding Box
 * (AABB) is visible within the view frustum. It uses the "positive vertex" method
 * for efficient conservative culling.
 *
 * Algorithm Overview:
 * -------------------
 * For each of the 6 frustum planes:
 * 1. Find the "positive vertex" (p-vertex) of the AABB
 * 2. Test if the p-vertex is outside the plane
 * 3. If p-vertex is outside, the entire AABB is outside (cull it)
 * 4. If ALL tests pass, the AABB is at least partially visible
 *
 * Positive Vertex Method:
 * -----------------------
 * The positive vertex (p-vertex) is the corner of the AABB that is "most aligned"
 * with the plane's normal (most in the direction the plane is facing).
 *
 * For each axis:
 * - If plane normal component ≥ 0: use max bound (plane faces positive direction)
 * - If plane normal component < 0: use min bound (plane faces negative direction)
 *
 * Example:
 * --------
 * Plane normal: (0.6, -0.8, 0)  [pointing right and down]
 * AABB min: (2, 5, 1), max: (4, 7, 3)
 * - X: normal.x = 0.6 ≥ 0  →  use max.x = 4
 * - Y: normal.y = -0.8 < 0  →  use min.y = 5
 * - Z: normal.z = 0 ≥ 0     →  use max.z = 3
 * - p-vertex = (4, 5, 3)
 *
 * Why P-Vertex Works:
 * -------------------
 * The p-vertex is the AABB corner closest to being in front of the plane.
 * If even the p-vertex is behind the plane, then ALL 8 corners must be behind,
 * so the entire AABB is outside the frustum.
 *
 * Visual Example:
 * ---------------
 * ```
 *        Plane normal →
 *                    |
 *    ┌───────┐       |
 *    │ AABB  │   p ← | (positive vertex)
 *    └───────┘       |
 *         ↑          |
 *         n          |  ← Plane
 *   (negative vertex)|
 * ```
 *
 * Conservative Culling:
 * ---------------------
 * This test is conservative: it may return true for AABBs that are partially
 * outside the frustum (false positives), but it will NEVER incorrectly cull
 * visible AABBs (no false negatives). This is acceptable because:
 * - GPU will clip partially-visible geometry anyway
 * - False positives have minimal performance impact
 * - False negatives would cause visible objects to disappear (unacceptable)
 *
 * Margin Parameter:
 * -----------------
 * The margin (default 2.0 world units) expands the frustum slightly to prevent
 * "popping" artifacts where chunks appear/disappear at screen edges.
 *
 * - Without margin: Chunks cull exactly at screen boundary (visible pop-in)
 * - With margin: Chunks remain rendered slightly beyond screen edge (smooth)
 *
 * The margin is subtracted from the distance test, effectively moving the planes
 * outward by `margin` units.
 *
 * Performance:
 * ------------
 * - Time complexity: O(1) - only 6 plane tests, no loops
 * - Early exit: Returns false as soon as one plane test fails
 * - Typical case: ~3-4 plane tests on average (early exit)
 * - Worst case: 6 plane tests if AABB is fully inside frustum
 *
 * Usage in Rendering Loop:
 * ------------------------
 * ```cpp
 * Frustum frustum = extractFrustum(viewProjMatrix);
 *
 * for (Chunk* chunk : chunks) {
 *     if (frustumAABBIntersect(frustum, chunk->getMin(), chunk->getMax(), 2.0f)) {
 *         chunk->render(commandBuffer);  // Visible, render it
 *     }
 *     // else: culled, skip rendering
 * }
 * ```
 *
 * Typical Performance Gains:
 * --------------------------
 * In a voxel world with view distance of 10 chunks (20x20 grid):
 * - Total chunks: 400
 * - Chunks after frustum culling: 60-100 (75-85% reduction)
 * - Chunks after distance culling: 20-40 (90-95% reduction)
 *
 * @param frustum The view frustum with 6 normalized planes
 * @param min Minimum corner of the AABB in world space
 * @param max Maximum corner of the AABB in world space
 * @param margin Expansion margin to prevent edge popping (default 2.0)
 * @return true if AABB is at least partially visible, false if completely culled
 */
inline bool frustumAABBIntersect(const Frustum& frustum, const glm::vec3& min, const glm::vec3& max, float margin = 2.0f) {
    // For each plane, test if the AABB is completely outside
    for (int i = 0; i < 6; ++i) {
        const Plane& plane = frustum.planes[i];

        // Find the positive vertex (vertex most aligned with plane normal)
        glm::vec3 pVertex;
        pVertex.x = (plane.a >= 0) ? max.x : min.x;
        pVertex.y = (plane.b >= 0) ? max.y : min.y;
        pVertex.z = (plane.c >= 0) ? max.z : min.z;

        // If the positive vertex is outside this plane (with margin), the entire AABB is outside
        // Margin prevents edge-case popping at frustum boundaries
        if (plane.distanceToPoint(pVertex) < -margin) {
            return false;
        }
    }

    // AABB is at least partially inside the frustum
    return true;
}
