#version 450

// ============================================================================
// COMPRESSED VERTEX SHADER (4x memory savings: 48 bytes -> 12 bytes per vertex)
// ============================================================================
// Based on Vercidium's voxel optimization technique:
// https://vercidium.com/blog/voxel-world-optimisations/
// ============================================================================

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 cameraPos;       // .xyz = camera position, .w = render distance
    vec4 skyTimeData;     // .x = time of day (0-1), .y = sun intensity, .z = moon intensity, .w = star intensity
    vec4 liquidFogColor;  // Liquid fog color (.rgb) + density (.a)
    vec4 liquidFogDist;   // Fog distances (.x=start, .y=end) + unused (.zw)
    vec4 liquidTint;      // Liquid tint color (.rgb) + darken factor (.a)
    vec4 atlasInfo;       // .x = atlas width in cells, .y = height, .z = cell size (1/width), .w = unused
} ubo;

// Compressed vertex input (12 bytes total)
layout(location = 0) in ivec3 inPosition;   // World position as signed 16-bit integers (6 bytes, padded to ivec3)
layout(location = 1) in uint inPackedA;     // Atlas X (8) + Atlas Y (8)
layout(location = 2) in uint inPackedB;     // Normal + QuadSize + Corner + Lighting + Tint

// Outputs to fragment shader
layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec3 fragWorldPos;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out float fragWaveIntensity;
layout(location = 4) out float fragSkyLight;
layout(location = 5) out float fragBlockLight;
layout(location = 6) out float fragAO;

// Color tint palette (4 tints to fit in 2 bits)
const vec4 TINT_PALETTE[4] = vec4[4](
    vec4(1.0, 1.0, 1.0, 1.0),       // 0: White (default)
    vec4(0.4, 0.6, 0.9, 0.7),       // 1: Water blue (translucent)
    vec4(0.5, 0.8, 0.4, 1.0),       // 2: Foliage green
    vec4(0.6, 0.85, 0.45, 1.0)      // 3: Grass green
);

void main() {
    // ========== UNPACK POSITION ==========
    // Position comes in as signed 16-bit integers (world coordinates)
    vec3 worldPos = vec3(float(inPosition.x), float(inPosition.y), float(inPosition.z));

    // ========== UNPACK ATLAS COORDS (packedA) ==========
    // Bits 0-7:   Atlas X cell
    // Bits 8-15:  Atlas Y cell
    float atlasX = float(inPackedA & 0xFFu);
    float atlasY = float((inPackedA >> 8u) & 0xFFu);

    // ========== UNPACK OTHER DATA (packedB) ==========
    // Bits 0-2:   Normal index (0-5) - unused for now but available
    // Bits 3-7:   Quad width (0-31)
    // Bits 8-12:  Quad height (0-31)
    // Bits 13-14: Corner index (0-3)
    // Bits 15-18: Sky light (0-15)
    // Bits 19-22: Block light (0-15)
    // Bits 23-26: AO (0-15)
    // Bits 27-28: Color tint (0-3)

    // uint normalIndex = inPackedB & 0x7u;  // Uncomment if needed
    float quadWidth = float((inPackedB >> 3u) & 0x1Fu);
    float quadHeight = float((inPackedB >> 8u) & 0x1Fu);
    uint cornerIndex = (inPackedB >> 13u) & 0x3u;
    float skyLight = float((inPackedB >> 15u) & 0xFu) / 15.0;
    float blockLight = float((inPackedB >> 19u) & 0xFu) / 15.0;
    float ao = float((inPackedB >> 23u) & 0xFu) / 15.0;
    uint colorTint = (inPackedB >> 27u) & 0x3u;

    // ========== RECONSTRUCT UV COORDINATES ==========
    // Corner index determines UV offset within the quad:
    // 0: (0, 0)                  1: (quadWidth, 0)
    // 2: (0, quadHeight)         3: (quadWidth, quadHeight)
    float uvOffsetU = ((cornerIndex & 1u) != 0u) ? quadWidth : 0.0;
    float uvOffsetV = ((cornerIndex & 2u) != 0u) ? quadHeight : 0.0;

    // Calculate final texture coordinates using tiled encoding
    // UV = cellIndex + localOffset / atlasSize
    // This allows shader to use fract() for texture tiling
    float atlasSize = max(ubo.atlasInfo.x, 1.0);  // Number of cells per row (default 1 to avoid div by 0)
    fragTexCoord = vec2(atlasX + uvOffsetU / atlasSize, atlasY + uvOffsetV / atlasSize);

    // ========== OUTPUT ==========
    // Apply model matrix (identity for world rendering)
    vec4 transformedPos = ubo.model * vec4(worldPos, 1.0);

    gl_Position = ubo.projection * ubo.view * transformedPos;
    fragWorldPos = transformedPos.xyz;
    fragColor = TINT_PALETTE[colorTint];
    fragWaveIntensity = (colorTint == 1u) ? 1.0 : 0.0;  // Water gets wave intensity
    fragSkyLight = skyLight;
    fragBlockLight = blockLight;
    fragAO = ao;
}
