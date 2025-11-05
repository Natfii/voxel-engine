#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 cameraPos;    // .xyz = camera position, .w = render distance
    vec4 skyTimeData;  // .x = time of day (0-1), .y = sun intensity, .z = moon intensity, .w = star intensity
} ubo;

layout(binding = 2) uniform samplerCube daySkybox;
layout(binding = 3) uniform samplerCube nightSkybox;

layout(location = 0) in vec3 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    // Sample both day and night cube maps
    vec3 daySkyColor = texture(daySkybox, fragTexCoord).rgb;
    vec3 nightSkyColor = texture(nightSkybox, fragTexCoord).rgb;

    // Time-based data
    float time = ubo.skyTimeData.x;
    float sunIntensity = ubo.skyTimeData.y;
    float moonIntensity = ubo.skyTimeData.z;
    float starIntensity = ubo.skyTimeData.w;

    // Sharper blend between day and night
    // Use smoothstep to make transitions cleaner
    float dayNightBlend = smoothstep(0.0, 0.15, sunIntensity);  // Sharp transition
    vec3 skyColor = mix(nightSkyColor, daySkyColor, dayNightBlend);

    // Add dynamic tinting for day sky only
    if (dayNightBlend > 0.1) {
        // Slight brightness boost for day sky (cube map already has blue)
        skyColor *= (1.0 + 0.15 * dayNightBlend);

        // Calculate whether we're in dawn/dusk period
        // Dawn: 0.15-0.35 (broader for longer effect)
        float dawnFactor = smoothstep(0.15, 0.25, time) * (1.0 - smoothstep(0.3, 0.4, time));
        // Dusk: 0.6-0.85 (broader for longer effect)
        float duskFactor = smoothstep(0.6, 0.7, time) * (1.0 - smoothstep(0.8, 0.9, time));
        float dawnDuskFactor = dawnFactor + duskFactor;

        if (dawnDuskFactor > 0.01) {
            // Gradient based on vertical position (y coordinate)
            float verticalGradient = normalize(fragTexCoord).y;
            verticalGradient = clamp(verticalGradient, -0.3, 1.0); // Clamp to avoid weird colors below horizon

            // Dreamy color palette for dawn/dusk
            vec3 horizonColor = vec3(1.0, 0.4, 0.2);    // Vibrant orange at horizon
            vec3 midColor = vec3(1.0, 0.6, 0.7);        // Pink/salmon in middle
            vec3 topColor = vec3(0.6, 0.4, 0.8);        // Purple at top

            // Blend colors based on vertical gradient
            vec3 dawnDuskColor;
            if (verticalGradient < 0.3) {
                // Horizon to mid: orange -> pink
                float t = smoothstep(-0.3, 0.3, verticalGradient);
                dawnDuskColor = mix(horizonColor, midColor, t);
            } else {
                // Mid to top: pink -> purple
                float t = smoothstep(0.3, 1.0, verticalGradient);
                dawnDuskColor = mix(midColor, topColor, t);
            }

            // Apply dreamy dawn/dusk tint
            skyColor = mix(skyColor, skyColor * dawnDuskColor * 1.5, dawnDuskFactor * dayNightBlend);
        }
    }

    // Calculate sun direction (moves across sky based on time)
    // At time 0.5 (noon), sun is at top
    // At time 0.0/1.0 (midnight), sun is below horizon
    float sunAngle = (time - 0.5) * 3.14159 * 2.0;  // -PI to PI
    vec3 sunDir = normalize(vec3(
        0.0,
        sin(sunAngle),
        cos(sunAngle)
    ));

    // Calculate moon direction (opposite to sun)
    vec3 moonDir = -sunDir;

    // Create perpendicular basis vectors for sun (for square rendering)
    vec3 sunRight = normalize(cross(sunDir, vec3(0.0, 1.0, 0.0)));
    vec3 sunUp = normalize(cross(sunRight, sunDir));

    // Add square sun with dreamy colors
    vec3 viewDir = normalize(fragTexCoord);
    float sunDot = dot(viewDir, sunDir);

    if (sunDot > 0.99 && sunIntensity > 0.01) {
        // Project view direction onto sun's perpendicular plane
        vec3 toView = viewDir - sunDir * sunDot;
        float sunX = dot(toView, sunRight);
        float sunY = dot(toView, sunUp);

        // Square sun (check if within square bounds)
        float sunSize = 0.015;  // Half-width of square
        if (abs(sunX) < sunSize && abs(sunY) < sunSize) {
            // Distance from center for glow effect
            float distFromCenter = max(abs(sunX), abs(sunY)) / sunSize;

            if (distFromCenter < 0.5) {
                // Bright core
                vec3 sunCore = vec3(1.0, 0.85, 0.6) * sunIntensity;
                skyColor += sunCore * 5.0 * (1.0 - distFromCenter * 2.0);
            } else {
                // Outer glow with orangey-purple gradient
                float glowFactor = 1.0 - (distFromCenter - 0.5) * 2.0;

                // Gradient from purple outer edge to orange inner
                vec3 outerColor = vec3(0.8, 0.4, 0.9);   // Purple
                vec3 innerColor = vec3(1.0, 0.6, 0.4);   // Orange
                vec3 glowColor = mix(outerColor, innerColor, 1.0 - distFromCenter);

                skyColor += glowColor * glowFactor * sunIntensity * 2.5;
            }
        }
    }

    // Create perpendicular basis vectors for moon (for square rendering)
    vec3 moonRight = normalize(cross(moonDir, vec3(0.0, 1.0, 0.0)));
    vec3 moonUp = normalize(cross(moonRight, moonDir));

    // Add square moon
    float moonDot = dot(viewDir, moonDir);
    if (moonDot > 0.99 && moonIntensity > 0.01) {
        // Project view direction onto moon's perpendicular plane
        vec3 toViewMoon = viewDir - moonDir * moonDot;
        float moonX = dot(toViewMoon, moonRight);
        float moonY = dot(toViewMoon, moonUp);

        // Square moon (check if within square bounds)
        float moonSize = 0.012;  // Half-width of square (slightly smaller than sun)
        if (abs(moonX) < moonSize && abs(moonY) < moonSize) {
            vec3 moonColor = vec3(0.8, 0.8, 1.0) * moonIntensity;
            skyColor += moonColor * 1.5;  // Dimmer than sun
        }
    }

    // Stars are now baked into the night cube map texture

    outColor = vec4(skyColor, 1.0);
}
