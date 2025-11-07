#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 cameraPos;    // .xyz = camera position, .w = render distance
    vec4 skyTimeData;  // .x = time of day (0-1), .y = sun intensity, .z = moon intensity, .w = star intensity
} ubo;

layout(binding = 1) uniform sampler2D texSampler;

layout(location = 0) in vec4 fragColor;  // Now vec4 with alpha
layout(location = 1) in vec3 fragWorldPos;
layout(location = 2) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    vec2 texCoord = fragTexCoord;

    // Liquid animation: if alpha < 1.0, apply diagonal scrolling
    if (fragColor.a < 0.99) {
        // Use time of day for animation
        float time = ubo.skyTimeData.x * 100.0;  // Scale time for visible movement

        // Diagonal scrolling (both U and V move)
        float scrollSpeed = 0.02;
        texCoord.x += time * scrollSpeed;
        texCoord.y += time * scrollSpeed * 0.7;  // Slightly different speed for diagonal effect

        // Keep UVs in valid range
        texCoord = fract(texCoord);
    }

    // Sample texture and multiply by vertex color
    // Textured blocks have white (1,1,1,1) vertex color → shows texture
    // Colored blocks have colored vertex color → shows solid color (default white texture)
    vec4 texColor = texture(texSampler, texCoord);
    vec3 baseColor = texColor.rgb * fragColor.rgb;

    // Extract camera position and render distance from packed vec4
    vec3 camPos = ubo.cameraPos.xyz;
    float renderDistance = ubo.cameraPos.w;

    // Extract sky time data
    float time = ubo.skyTimeData.x;
    float sunIntensity = ubo.skyTimeData.y;
    float moonIntensity = ubo.skyTimeData.z;

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

    const float fogStart = renderDistance * 0.7;   // Fog starts at 70% of render distance
    const float fogEnd = renderDistance * 0.95;    // Full fog at 95% of render distance

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
    finalColor *= ambientLight;

    // Output with alpha from vertex color (for liquid transparency)
    outColor = vec4(finalColor, fragColor.a);
}
