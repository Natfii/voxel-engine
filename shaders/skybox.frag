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
float hash(vec2 p) {
    p = fract(p * vec2(123.45, 456.78));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

// Generate procedural stars with colors - spherical coordinate approach
vec3 stars(vec3 dir) {
    // Only render stars in upper hemisphere
    if (dir.y < 0.0) return vec3(0.0);

    // Convert to spherical coordinates for even distribution
    float phi = atan(dir.z, dir.x);  // Azimuthal angle
    float theta = asin(dir.y);        // Elevation angle

    // Create grid in angular space (evenly distributed on sphere)
    vec2 angleCoord = vec2(phi, theta) * 50.0;  // 50 cells per radian
    vec2 cellCoord = floor(angleCoord);

    // Check multiple nearby cells to avoid gaps
    vec3 starColor = vec3(0.0);

    // Near high elevations (theta > 0.5 radians ~30 degrees), only check current cell to avoid stretching
    // This is aggressive but prevents the zenith singularity artifacts
    int phiRange = (abs(theta) > 0.5) ? 0 : 1;

    // Near zenith, search more theta cells to compensate for no phi searching
    int thetaRange = (abs(theta) > 0.5) ? 3 : 1;

    for (int dx = -phiRange; dx <= phiRange; dx++) {
        for (int dy = -thetaRange; dy <= thetaRange; dy++) {
            vec2 checkCell = cellCoord + vec2(dx, dy);
            float h = hash(checkCell);

            if (h > 0.99) {  // 1% chance per cell
                // Random offset within this cell
                vec2 offset = vec2(
                    fract(h * 41.123),
                    fract(h * 73.456)
                );
                vec2 starAngle = (checkCell + offset) / 50.0;

                // Convert back to 3D direction
                float starPhi = starAngle.x;
                float starTheta = clamp(starAngle.y, -1.57, 1.57);  // Clamp to avoid invalid values

                vec3 starDir = normalize(vec3(
                    cos(starTheta) * cos(starPhi),
                    sin(starTheta),
                    cos(starTheta) * sin(starPhi)
                ));

                // Angular distance to this star
                float angularDist = acos(clamp(dot(dir, starDir), -1.0, 1.0));

                // Extremely tiny star size - 0.00000025 to 0.0000004 radians (2000x smaller than original)
                float starSize = 0.00000025 + fract(h * 97.789) * 0.00000015;

                if (angularDist < starSize) {
                    float brightness = (1.0 - angularDist / starSize);

                    // Twinkle
                    float twinkle = 0.7 + 0.3 * sin(h * 100.0 + ubo.skyTimeData.x * 6.28);
                    brightness *= twinkle;

                    // Determine star color
                    float colorHash = fract(h * 43.7584);
                    vec3 color;
                    if (colorHash < 0.15) {
                        color = vec3(1.0, 0.6, 0.6);  // Red
                    } else if (colorHash < 0.30) {
                        color = vec3(0.6, 0.7, 1.0);  // Blue
                    } else {
                        color = vec3(0.95, 0.95, 1.0);  // White
                    }

                    starColor = max(starColor, color * brightness);
                }
            }
        }
    }

    return starColor;
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
