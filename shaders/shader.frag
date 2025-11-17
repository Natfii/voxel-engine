#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 cameraPos;       // .xyz = camera position, .w = render distance
    vec4 skyTimeData;     // .x = time of day (0-1), .y = sun intensity, .z = moon intensity, .w = underwater 0/1
    vec4 liquidFogColor;  // .rgb = fog color, .a = fog density
    vec4 liquidFogDist;   // .x = fog start, .y = fog end, .zw = unused
    vec4 liquidTint;      // .rgb = tint color, .a = darken factor
} ubo;

layout(binding = 1) uniform sampler2D texSampler;

layout(location = 0) in vec4 fragColor;  // Now vec4 with alpha
layout(location = 1) in vec3 fragWorldPos;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in float fragWaveIntensity;

layout(location = 0) out vec4 outColor;

void main() {
    vec2 texCoord = fragTexCoord;

    // Parallax scrolling ONLY for water (not ice or other transparent blocks)
    // Water has transparency=0.25 (alpha=0.75), ice has transparency=0.4 (alpha=0.6)
    if (fragColor.a > 0.7 && fragColor.a < 0.8) {  // Only water blocks
        // Water is in an atlas, need to wrap within cell boundaries
        const float atlasSize = 4.0;  // 4x4 atlas
        const float cellSize = 1.0 / atlasSize;

        // Find which atlas cell we're in
        vec2 cellIndex = floor(texCoord * atlasSize);

        // Convert to cell-local coordinates (0-1 within cell)
        vec2 localUV = fract(texCoord * atlasSize);

        // Apply diagonal scrolling to local coordinates
        float scrollSpeed = 250.0;  // Fast, smooth flow
        localUV.y -= ubo.skyTimeData.x * scrollSpeed;        // Downward (subtract to flow down)
        localUV.x += ubo.skyTimeData.x * scrollSpeed * 0.5;  // Diagonal drift

        // Wrap within cell (seamless tiling)
        localUV = fract(localUV);

        // Convert back to global atlas coordinates
        texCoord = (cellIndex + localUV) * cellSize;
    }

    // Sample texture and multiply by vertex color
    // Textured blocks have white (1,1,1,1) vertex color → shows texture
    // Colored blocks have colored vertex color → shows solid color (default white texture)
    vec4 texColor = texture(texSampler, texCoord);
    vec3 baseColor = texColor.rgb * fragColor.rgb;

    // Darken and tint water to reduce bright patches (not too dark)
    if (fragColor.a > 0.7 && fragColor.a < 0.8) {  // Only liquid blocks (water/lava)
        baseColor *= 0.65;  // Darken by 35% (less aggressive)
        baseColor *= vec3(0.75, 0.9, 1.0);  // Subtle blue tint
    }

    // Foam effect disabled - didn't fit the aesthetic
    // Clean water surface with just waves and flowing texture

    // Extract camera position and render distance from packed vec4
    vec3 camPos = ubo.cameraPos.xyz;
    float renderDistance = ubo.cameraPos.w;

    // Extract sky time data
    float time = ubo.skyTimeData.x;
    float sunIntensity = ubo.skyTimeData.y;
    float moonIntensity = ubo.skyTimeData.z;
    bool cameraUnderwater = (ubo.skyTimeData.w > 0.5);  // Passed from CPU

    // Calculate distance from camera to fragment
    float distance = length(fragWorldPos - camPos);

    // Dynamic fog color based on time of day
    vec3 dayFogColor = vec3(0.7, 0.85, 1.0);       // Light blue (day)
    vec3 dawnDuskFogColor = vec3(1.0, 0.7, 0.5);   // Orange/pink (dawn/dusk)
    vec3 nightFogColor = vec3(0.02, 0.02, 0.02);   // Nearly black (night)

    // Calculate dawn/dusk factor
    float dawnDuskFactor = smoothstep(0.2, 0.3, time) * (1.0 - smoothstep(0.35, 0.45, time));
    dawnDuskFactor += smoothstep(0.65, 0.75, time) * (1.0 - smoothstep(0.8, 0.9, time));

    // Blend fog colors based on time of day
    vec3 fogColor = mix(dayFogColor, dawnDuskFogColor, dawnDuskFactor);
    fogColor = mix(fogColor, nightFogColor, moonIntensity * 0.8);

    // Fog settings (default above water)
    // Adjusted for better visibility at long distances
    float fogStart = renderDistance * 0.5;   // Fog starts at 50% of render distance
    float fogEnd = renderDistance * 0.90;    // Full fog at 90% of render distance

    // Underwater fog settings (use dynamic liquid properties from YAML)
    if (cameraUnderwater) {
        fogColor = ubo.liquidFogColor.rgb;   // Custom fog color from liquid YAML
        fogStart = ubo.liquidFogDist.x;       // Custom fog start distance
        fogEnd = ubo.liquidFogDist.y;         // Custom fog end distance
    }

    // Discard fragments well beyond fog end to avoid visible banding
    if (distance > renderDistance * 1.05) {
        discard;
    }

    // Only apply fog if we're in the fog range
    vec3 finalColor = baseColor;
    if (distance > fogStart) {
        // Calculate linear fog factor (1.0 = no fog, 0.0 = full fog)
        float fogFactor = clamp((fogEnd - distance) / (fogEnd - fogStart), 0.0, 1.0);
        // Mix block color with dynamic fog color
        finalColor = mix(fogColor, baseColor, fogFactor);
    }

    // Apply time-of-day lighting
    float ambientLight = 0.3 + 0.7 * sunIntensity + 0.15 * moonIntensity;

    // Underwater lighting - use dynamic liquid properties
    if (cameraUnderwater) {
        // Reduce ambient light underwater using custom darken factor
        ambientLight *= ubo.liquidTint.a;  // Custom darken factor from YAML

        // Add depth-based darkening (deeper = darker)
        float depthFactor = clamp(distance / 10.0, 0.0, 0.8);
        ambientLight *= (1.0 - depthFactor * 0.5);

        // Apply custom tint color that increases with depth
        finalColor = mix(finalColor, finalColor * ubo.liquidTint.rgb, depthFactor);
    }

    finalColor *= ambientLight;

    // Output with alpha from vertex color (for liquid transparency)
    outColor = vec4(finalColor, fragColor.a);
}
