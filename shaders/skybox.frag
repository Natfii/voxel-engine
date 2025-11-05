#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 cameraPos;    // .xyz = camera position, .w = render distance
    vec4 skyTimeData;  // .x = time of day (0-1), .y = sun intensity, .z = moon intensity, .w = star intensity
} ubo;

layout(binding = 2) uniform samplerCube skybox;

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

    if (h > 0.98) {  // Only 2% of cells have stars
        vec3 starPos = cellCoord + vec3(0.5);
        vec3 toStar = normalize(starPos - starCoord);
        float dist = distance(normalize(starPos), dir);

        // Make stars twinkle slightly
        float twinkle = 0.8 + 0.2 * sin(h * 100.0 + ubo.skyTimeData.x * 6.28);

        if (dist < 0.01) {
            return (1.0 - dist / 0.01) * twinkle;
        }
    }

    return 0.0;
}

void main() {
    // Sample cube map for base sky color
    vec3 skyColor = texture(skybox, fragTexCoord).rgb;

    // Time-based color adjustments
    float time = ubo.skyTimeData.x;
    float sunIntensity = ubo.skyTimeData.y;
    float moonIntensity = ubo.skyTimeData.z;
    float starIntensity = ubo.skyTimeData.w;

    // Calculate time-based sky tint
    // Dawn/dusk: orange/red tint
    // Noon: bright blue
    // Night: very dark blue
    vec3 dayTint = vec3(1.5, 1.8, 2.2);        // Bright blue day sky
    vec3 dawnDuskTint = vec3(2.0, 1.2, 0.8);   // Warm orange/pink
    vec3 nightTint = vec3(0.5, 0.6, 0.9);      // Dark blue night

    // Calculate whether we're in dawn/dusk period
    float dawnDuskFactor = smoothstep(0.2, 0.3, time) * (1.0 - smoothstep(0.35, 0.45, time));
    dawnDuskFactor += smoothstep(0.65, 0.75, time) * (1.0 - smoothstep(0.8, 0.9, time));

    vec3 timeTint = mix(dayTint, dawnDuskTint, dawnDuskFactor);
    timeTint = mix(timeTint, nightTint, moonIntensity * 0.9);

    skyColor *= timeTint;

    // Aggressive darkening during night
    skyColor *= (0.15 + 0.85 * sunIntensity + 0.15 * moonIntensity);

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

    // Add sun
    float sunDot = dot(normalize(fragTexCoord), sunDir);
    if (sunDot > 0.999 && sunIntensity > 0.01) {  // Sun is very bright and small
        float sunGlow = smoothstep(0.999, 1.0, sunDot);
        vec3 sunColor = vec3(1.0, 1.0, 0.9) * sunGlow * sunIntensity;
        skyColor += sunColor * 3.0;  // Bright sun
    } else if (sunDot > 0.99 && sunIntensity > 0.01) {
        // Sun corona/glow
        float coronaGlow = smoothstep(0.99, 0.999, sunDot);
        skyColor += vec3(1.0, 0.9, 0.7) * coronaGlow * sunIntensity * 0.5;
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
        skyColor += vec3(1.0) * starBrightness * starIntensity * 0.8;
    }

    outColor = vec4(skyColor, 1.0);
}
