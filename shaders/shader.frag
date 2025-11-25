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
layout(location = 4) in float fragSkyLight;    // Sky light from vertex shader
layout(location = 5) in float fragBlockLight;  // Block light from vertex shader
layout(location = 6) in float fragAO;          // Ambient occlusion from vertex shader

layout(location = 0) out vec4 outColor;

void main() {
    const float atlasSize = 4.0;  // 4x4 atlas
    const float cellSize = 1.0 / atlasSize;
    vec2 texCoord = fragTexCoord;

    // Parallax scrolling ONLY for water (not ice or other transparent blocks)
    // Water has transparency=0.25 (alpha=0.75), ice has transparency=0.4 (alpha=0.6)
    // NOTE: Water uses OLD stretched UV encoding (animatedTiles > 1 in mesh gen)
    if (fragColor.a > 0.7 && fragColor.a < 0.8) {  // Only water blocks
        // Find which atlas cell we're in (old encoding: UVs in 0-1 atlas space)
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
    else {
        // TILED UV DECODING for greedy meshing
        // UV encoding: cellIndex + (localUV * quadSize) / atlasSize
        // - Integer part = atlas cell index (0-3 for 4x4 atlas)
        // - Fractional part * atlasSize = local position (0 to quadSize)
        // Using fract() on local position gives automatic tiling
        vec2 cell = floor(fragTexCoord);                   // Which atlas cell
        vec2 localUV = fract(fragTexCoord) * atlasSize;    // Position scaled by quad size
        vec2 tiledUV = fract(localUV);                     // Tile within 0-1
        texCoord = (cell + tiledUV) * cellSize;            // Convert back to atlas space
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

    // ATMOSPHERIC FOG SYSTEM - Puffy, cloud-like fog with height attenuation
    // Three components combine for realistic atmospheric scattering:
    // 1. Distance fog: exponential falloff (natural looking, no hard edges)
    // 2. Height fog: thicker at low altitudes (ground haze), thinner above
    // 3. Horizon fog: extra density at far horizontal distances (hides distant mountains)

    if (cameraUnderwater) {
        // Underwater: use simple linear fog from YAML settings
        float underwaterFog = clamp((distance - fogStart) / (fogEnd - fogStart), 0.0, 1.0);
        finalColor = mix(finalColor, fogColor, underwaterFog);
    } else {
        // Above water: atmospheric fog with height and horizon effects
        float worldY = fragWorldPos.y;
        float heightFogCenter = 80.0;   // Sea level fog is thickest
        float heightFogFalloff = 0.008; // How quickly fog clears with altitude
        float heightDiff = abs(worldY - heightFogCenter);
        float heightFogFactor = exp(-heightDiff * heightFogFalloff);  // 1.0 at sea level, fades away

        // Distance fog with exponential falloff (soft, natural edges)
        float fogDensity = 2.5 / renderDistance;  // Scales with render distance
        float distanceFog = 1.0 - exp(-distance * fogDensity);

        // Horizon fog: extra density for distant objects near the horizon
        // This hides mountains that peek through when looking up
        vec3 viewDir = normalize(fragWorldPos - camPos);
        float horizonFactor = 1.0 - abs(viewDir.y);  // 1.0 when looking horizontal, 0.0 when looking up/down
        horizonFactor = horizonFactor * horizonFactor;  // Squared for sharper falloff
        float horizonFog = horizonFactor * distanceFog * 0.4;  // Extra 40% fog at horizon

        // Combine fog factors (multiplicative for height, additive for horizon)
        float combinedFog = distanceFog * (0.6 + 0.4 * heightFogFactor) + horizonFog;
        combinedFog = clamp(combinedFog, 0.0, 1.0);

        // Apply fog (mix between scene color and fog color based on fog density)
        finalColor = mix(finalColor, fogColor, combinedFog);
    }

    // Output with alpha from vertex color (for liquid transparency)
    outColor = vec4(finalColor, fragColor.a);
}
