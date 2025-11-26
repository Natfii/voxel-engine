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
    vec4 atlasInfo;       // .x = atlas width in cells, .y = height, .z = cell size (1/width), .w = unused
} ubo;

layout(binding = 1) uniform sampler2D texSampler;

layout(location = 0) in vec4 fragColor;  // Now vec4 with alpha
layout(location = 1) in vec3 fragWorldPos;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in float fragWaveIntensity;
layout(location = 4) in float fragSkyLight;    // Sky light from vertex shader
layout(location = 5) in float fragBlockLight;  // Block light from vertex shader
layout(location = 6) in float fragAO;          // Ambient occlusion from vertex shader

layout(location = 0) out vec4 outColor;

void main() {
    // Get atlas size from UBO (dynamic, not hardcoded)
    float atlasSize = max(ubo.atlasInfo.x, 1.0);
    float cellSize = 1.0 / atlasSize;
    vec2 texCoord = fragTexCoord;

    // Parallax scrolling ONLY for water (not ice or other transparent blocks)
    // Water has alpha=0.7 from TINT_PALETTE in vertex shader
    // Uses TILED UV encoding: integer part = atlas cell, fractional part = tiled position
    if (fragColor.a >= 0.65 && fragColor.a < 0.75) {  // Only water blocks (alpha=0.7)
        // TILED UV DECODING for water (same as other blocks but with parallax)
        vec2 cell = floor(fragTexCoord);                   // Which atlas cell
        vec2 localUV = fract(fragTexCoord) * atlasSize;    // Position scaled by quad size

        // Apply parallax scrolling BEFORE tiling
        float scrollSpeed = 250.0;  // Fast, smooth flow
        localUV.y -= ubo.skyTimeData.x * scrollSpeed;        // Downward flow
        localUV.x += ubo.skyTimeData.x * scrollSpeed * 0.5;  // Diagonal drift

        // Tile within 0-1 (handles both scrolling wrap and greedy mesh tiling)
        vec2 tiledUV = mod(localUV, 1.0);
        tiledUV = clamp(tiledUV, 0.001, 0.999);

        // Convert back to atlas space
        texCoord = (cell + tiledUV) * cellSize;
    }
    else {
        // TILED UV DECODING for greedy meshing
        // UV encoding: cellIndex + (localUV * quadSize) / atlasSize
        // - Integer part = atlas cell index (0-3 for 4x4 atlas)
        // - Fractional part * atlasSize = local position (0 to quadSize)
        // Using mod() for tiling to handle edge case where fract(1.0) = 0.0
        vec2 cell = floor(fragTexCoord);                   // Which atlas cell
        vec2 localUV = fract(fragTexCoord) * atlasSize;    // Position scaled by quad size
        // Use mod() instead of fract() to properly tile - handles exact integer values
        // For single blocks: localUV goes from 0 to 1, we sample 0 to 0.999...
        // For merged quads: localUV goes from 0 to N, texture tiles N times
        vec2 tiledUV = mod(localUV, 1.0);                  // Tile within 0-1
        // Clamp to avoid sampling neighboring cells at exact boundary
        tiledUV = clamp(tiledUV, 0.001, 0.999);
        texCoord = (cell + tiledUV) * cellSize;            // Convert back to atlas space
    }

    // Sample texture and multiply by vertex color
    // Textured blocks have white (1,1,1,1) vertex color → shows texture
    // Colored blocks have colored vertex color → shows solid color (default white texture)
    vec4 texColor = texture(texSampler, texCoord);
    vec3 baseColor = texColor.rgb * fragColor.rgb;

    // Darken and tint water to reduce bright patches (not too dark)
    if (fragColor.a >= 0.65 && fragColor.a < 0.75) {  // Only water blocks (alpha=0.7)
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
    // Pushed way out for clear visibility (2025-11-25)
    float fogStart = renderDistance * 0.85;   // Fog starts at 85% of render distance
    float fogEnd = renderDistance * 0.98;     // Full fog at 98% of render distance

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

    // VIEWPORT-BASED DUAL-CHANNEL LIGHTING SYSTEM
    // Sky light: Affected by sun/moon position (recalculated for visible chunks only)
    // Block light: Constant (torches, lava) regardless of time of day

    // Calculate sun/moon contribution to sky-lit blocks
    // During day: skyLight=1.0 blocks are BRIGHT (sun)
    // During night: skyLight=1.0 blocks are DIM (moon is weak)
    float sunContribution = sunIntensity;              // 1.0 at noon, 0.0 at night
    float moonContribution = moonIntensity * 0.25;     // Moon is much dimmer than sun
    float skyLightIntensity = sunContribution + moonContribution;

    // DIRECTIONAL SUN LIGHTING - Classic Minecraft-style face shading
    // Calculate face normal from derivatives (works for flat-shaded blocks)
    vec3 dFdxPos = dFdx(fragWorldPos);
    vec3 dFdyPos = dFdy(fragWorldPos);
    // FIX: Negate the cross product to get correct front-facing normals
    // Without negation, top faces appear dark and bottom faces appear bright (lit from below)
    vec3 faceNormal = -normalize(cross(dFdxPos, dFdyPos));

    // Sun always comes from above in retro style
    vec3 sunDirection = vec3(0.0, 1.0, 0.0);  // Pointing straight up

    // Calculate how much this face faces the sun (0.0 = perpendicular, 1.0 = directly facing)
    float sunDot = max(dot(faceNormal, sunDirection), 0.0);

    // Classic Minecraft face brightness multipliers:
    // Top faces: 100% brightness
    // Side faces: 80% brightness
    // Bottom faces: 60% brightness
    float faceBrightness = 0.6 + (sunDot * 0.4);  // Range: 0.6 to 1.0

    // Apply directional shading to sky light only (not block light/torches)
    float skyLightFinal = fragSkyLight * skyLightIntensity * faceBrightness;

    // Block light is constant (torches don't care about time of day or face direction)
    float blockLightFinal = fragBlockLight;

    // Ambient light prevents pure darkness (starlight, eye adjustment)
    float ambientLight = 0.15;  // LIGHTING FIX: Increased from 0.05 to 0.15 (15% ambient)

    // Combine using MAX (not add, to avoid over-brightening when torch + sunlight)
    float finalLight = max(max(skyLightFinal, blockLightFinal), ambientLight);

    // Apply lighting and ambient occlusion to base color FIRST
    vec3 finalColor = baseColor * finalLight * fragAO;

    // Underwater lighting - use dynamic liquid properties
    if (cameraUnderwater) {
        // Reduce all light underwater using custom darken factor
        finalColor *= ubo.liquidTint.a;  // Custom darken factor from YAML

        // Add depth-based darkening (deeper = darker)
        float depthFactor = clamp(distance / 10.0, 0.0, 0.8);
        finalColor *= (1.0 - depthFactor * 0.5);

        // Apply custom tint color that increases with depth
        finalColor = mix(finalColor, finalColor * ubo.liquidTint.rgb, depthFactor);
    }

    // SIMPLE LINEAR FOG (2025-11-25)
    // Clean Minecraft-style fog: no fog until fogStart, then linear blend to fogEnd
    float fogFactor = clamp((distance - fogStart) / (fogEnd - fogStart), 0.0, 1.0);
    finalColor = mix(finalColor, fogColor, fogFactor);

    // Output with alpha from vertex color (for liquid transparency)
    outColor = vec4(finalColor, fragColor.a);
}
