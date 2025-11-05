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

// Hash function for procedural stars
float hash(vec3 p) {
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

// Generate procedural stars
float stars(vec3 dir) {
    // Only render stars in upper hemisphere
    if (dir.y < 0.0) return 0.0;

    vec3 starCoord = dir * 100.0;
    vec3 cellCoord = floor(starCoord);

    // Generate a star in each cell with some probability
    float h = hash(cellCoord);

    if (h > 0.97) {  // 3% of cells have stars (good balance!)
        vec3 starPos = cellCoord + vec3(0.5);
        vec3 toStar = normalize(starPos - starCoord);
        float dist = distance(normalize(starPos), dir);

        // Vary star sizes - some bigger, some smaller
        float starSize = 0.012 + (h - 0.97) * 0.13;  // 0.012 to 0.016

        // Make stars twinkle slightly
        float twinkle = 0.7 + 0.3 * sin(h * 100.0 + ubo.skyTimeData.x * 6.28);

        if (dist < starSize) {
            float brightness = (1.0 - dist / starSize) * twinkle;
            // Vary brightness - some stars brighter than others
            brightness *= (0.5 + (h - 0.97) * 16.67);  // 0.5 to 1.0 brightness
            return brightness;
        }
    }

    return 0.0;
}

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

    // Add blue tint and dynamic tinting for day sky only
    if (dayNightBlend > 0.1) {
        // Brighter, more vibrant blue tint for day sky
        skyColor *= vec3(0.85, 1.0, 1.35) * (1.0 + 0.15 * dayNightBlend);

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

    // Add sun with dreamy colors
    float sunDot = dot(normalize(fragTexCoord), sunDir);
    if (sunDot > 0.996 && sunIntensity > 0.01) {
        // Dreamy sun with orangey-purple gradient
        if (sunDot > 0.998) {
            // Bright core with warm orange-yellow
            float coreGlow = smoothstep(0.998, 1.0, sunDot);
            vec3 sunCore = vec3(1.0, 0.85, 0.6) * coreGlow * sunIntensity;  // Warm orange-yellow
            skyColor += sunCore * 5.0;  // Very bright core
        } else if (sunDot > 0.996) {
            // Outer glow with orangey-purple gradient
            float glowAmount = (sunDot - 0.996) / 0.002;  // 0 to 1
            float glowFactor = smoothstep(0.0, 1.0, glowAmount);

            // Gradient from purple outer edge to orange inner
            vec3 outerColor = vec3(0.8, 0.4, 0.9);   // Purple
            vec3 innerColor = vec3(1.0, 0.6, 0.4);   // Orange
            vec3 glowColor = mix(outerColor, innerColor, glowFactor);

            skyColor += glowColor * glowFactor * sunIntensity * 2.5;
        }
    }

    // Add moon
    float moonDot = dot(normalize(fragTexCoord), moonDir);
    if (moonDot > 0.998 && moonIntensity > 0.01) {
        float moonGlow = smoothstep(0.998, 1.0, moonDot);
        vec3 moonColor = vec3(0.8, 0.8, 1.0) * moonGlow * moonIntensity;
        skyColor += moonColor * 1.5;  // Dimmer than sun
    }

    // Add stars at night
    if (starIntensity > 0.01) {
        float starBrightness = stars(fragTexCoord);
        // Brighter stars with slight blue tint
        skyColor += vec3(0.9, 0.95, 1.0) * starBrightness * starIntensity * 2.0;
    }

    outColor = vec4(skyColor, 1.0);
}
