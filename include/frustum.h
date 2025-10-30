#pragma once

#include <glm/glm.hpp>

// Represents a plane in 3D space using the equation: Ax + By + Cz + D = 0
struct Plane {
    float a, b, c, d;

    // Normalize the plane equation
    void normalize() {
        float mag = std::sqrt(a * a + b * b + c * c);
        if (mag > 0.0f) {
            a /= mag;
            b /= mag;
            c /= mag;
            d /= mag;
        }
    }

    // Calculate signed distance from point to plane
    float distanceToPoint(const glm::vec3& point) const {
        return a * point.x + b * point.y + c * point.z + d;
    }
};

// Frustum represented by 6 planes: left, right, bottom, top, near, far
struct Frustum {
    Plane planes[6];

    enum { LEFT = 0, RIGHT, BOTTOM, TOP, NEAR, FAR };
};

// Extract frustum planes from view-projection matrix
inline Frustum extractFrustum(const glm::mat4& viewProj) {
    Frustum frustum;

    // Extract planes from view-projection matrix
    // Left plane
    frustum.planes[Frustum::LEFT].a = viewProj[0][3] + viewProj[0][0];
    frustum.planes[Frustum::LEFT].b = viewProj[1][3] + viewProj[1][0];
    frustum.planes[Frustum::LEFT].c = viewProj[2][3] + viewProj[2][0];
    frustum.planes[Frustum::LEFT].d = viewProj[3][3] + viewProj[3][0];

    // Right plane
    frustum.planes[Frustum::RIGHT].a = viewProj[0][3] - viewProj[0][0];
    frustum.planes[Frustum::RIGHT].b = viewProj[1][3] - viewProj[1][0];
    frustum.planes[Frustum::RIGHT].c = viewProj[2][3] - viewProj[2][0];
    frustum.planes[Frustum::RIGHT].d = viewProj[3][3] - viewProj[3][0];

    // Bottom plane
    frustum.planes[Frustum::BOTTOM].a = viewProj[0][3] + viewProj[0][1];
    frustum.planes[Frustum::BOTTOM].b = viewProj[1][3] + viewProj[1][1];
    frustum.planes[Frustum::BOTTOM].c = viewProj[2][3] + viewProj[2][1];
    frustum.planes[Frustum::BOTTOM].d = viewProj[3][3] + viewProj[3][1];

    // Top plane
    frustum.planes[Frustum::TOP].a = viewProj[0][3] - viewProj[0][1];
    frustum.planes[Frustum::TOP].b = viewProj[1][3] - viewProj[1][1];
    frustum.planes[Frustum::TOP].c = viewProj[2][3] - viewProj[2][1];
    frustum.planes[Frustum::TOP].d = viewProj[3][3] - viewProj[3][1];

    // Near plane
    frustum.planes[Frustum::NEAR].a = viewProj[0][3] + viewProj[0][2];
    frustum.planes[Frustum::NEAR].b = viewProj[1][3] + viewProj[1][2];
    frustum.planes[Frustum::NEAR].c = viewProj[2][3] + viewProj[2][2];
    frustum.planes[Frustum::NEAR].d = viewProj[3][3] + viewProj[3][2];

    // Far plane
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

// Test if axis-aligned bounding box intersects with frustum
// Returns true if the box is at least partially inside the frustum
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
