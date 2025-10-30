#pragma once

// pull in the real std math functions
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// 3-component vector
struct Vec3 {
    float x, y, z;
};

// 4×4 matrix in column-major order
struct Mat4 {
    float m[16];
};

// normalize a vector
inline Vec3 normalize(const Vec3& v) {
    float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return { v.x / len, v.y / len, v.z / len };
}

// cross-product
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

// dot-product
inline float dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

// multiply two 4×4 matrices
inline Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int i = 0; i < 16; ++i) r.m[i] = 0.0f;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 4; ++k)
                r.m[i * 4 + j] += a.m[i * 4 + k] * b.m[k * 4 + j];
    return r;
}

// perspective projection matrix
inline Mat4 perspective(float fov, float aspect, float near, float far) {
    Mat4 r;
    for (int i = 0; i < 16; ++i) r.m[i] = 0.0f;
    float tanHalfFovy = std::tan(fov / 2.0f);
    r.m[0]  = 1.0f / (aspect * tanHalfFovy);
    r.m[5]  = 1.0f / tanHalfFovy;
    r.m[10] = -(far + near) / (far - near);
    r.m[11] = -1.0f;
    r.m[14] = -(2.0f * far * near) / (far - near);
    return r;
}

// look-at (view) matrix
inline Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
    Vec3 f = normalize({ center.x - eye.x,
                         center.y - eye.y,
                         center.z - eye.z });
    Vec3 s = normalize(cross(f, up));
    Vec3 u = cross(s, f);

    Mat4 r;
    for (int i = 0; i < 16; ++i) r.m[i] = 0.0f;
    r.m[0]  = s.x;  r.m[1]  = u.x;  r.m[2]  = -f.x; r.m[3]  = 0.0f;
    r.m[4]  = s.y;  r.m[5]  = u.y;  r.m[6]  = -f.y; r.m[7]  = 0.0f;
    r.m[8]  = s.z;  r.m[9]  = u.z;  r.m[10] = -f.z; r.m[11] = 0.0f;
    r.m[12] = -dot(s, eye);
    r.m[13] = -dot(u, eye);
    r.m[14] =  dot(f, eye);
    r.m[15] = 1.0f;
    return r;
}
