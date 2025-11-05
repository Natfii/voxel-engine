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

// Hash function for procedural stars - better distribution
float hash(vec3 p) {
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.x + p.y) * p.z);
}

// Generate procedural stars with colors
vec3 stars(vec3 dir) {
    // Only render stars in upper hemisphere
    if (dir.y < 0.0) return vec3(0.0);

    // Much denser grid for better distribution
    vec3 starCoord = dir * 300.0;  // Increased from 100 to 300
    vec3 cellCoord = floor(starCoord);

    // Generate a star in each cell with some probability
    float h = hash(cellCoord);

    if (h > 0.985) {  // 1.5% of cells have stars
        // Randomize position within cell for less grid-like appearance
        vec3 offset = vec3(
            fract(h * 41.123),
            fract(h * 73.456),
            fract(h * 97.789)
        );
        vec3 starPos = cellCoord + offset;
        float dist = distance(normalize(starPos), dir);

        // Vary star sizes - some bigger, some smaller
        float starSize = 0.004 + (h - 0.985) * 0.08;  // Smaller to match denser grid

        // Make stars twinkle slightly
        float twinkle = 0.7 + 0.3 * sin(h * 100.0 + ubo.skyTimeData.x * 6.28);

        if (dist < starSize) {
            float brightness = (1.0 - dist / starSize) * twinkle;
            // Vary brightness - some stars brighter than others
            brightness *= (0.5 + (h - 0.985) * 33.33);  // 0.5 to 1.0 brightness

            // Determine star color based on hash value
            float colorHash = fract(h * 43.7584);  // Different hash for color
            vec3 starColor;
            if (colorHash < 0.15) {
                // Red stars (15%)
                starColor = vec3(1.0, 0.6, 0.6);
            } else if (colorHash < 0.30) {
                // Blue stars (15%)
                starColor = vec3(0.6, 0.7, 1.0);
            } else {
                // White stars (70%)
                starColor = vec3(0.95, 0.95, 1.0);
            }

            return starColor * brightness;
        }
    }

    return vec3(0.0);
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

    // Add stars at night
    if (starIntensity > 0.01) {
        vec3 starColor = stars(fragTexCoord);
        // Stars now have their own colors (red, blue, white)
        skyColor += starColor * starIntensity * 2.0;
    }

    outColor = vec4(skyColor, 1.0);
}
