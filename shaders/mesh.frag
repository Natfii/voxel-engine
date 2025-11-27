#version 450

// ========== Inputs ==========

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragTangent;
layout(location = 4) in vec3 fragBitangent;
layout(location = 5) in vec4 fragTintColor;
layout(location = 6) in float fragTimeOfDay;

// ========== Outputs ==========

layout(location = 0) out vec4 outColor;

// ========== Uniforms ==========

// Set 0: Camera data (shared with voxel pipeline)
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 cameraPos;
    vec4 skyTimeData;
    vec4 liquidFogColor;
    vec4 liquidFogDist;
    vec4 liquidTint;
} camera;

// Set 1: Mesh textures
layout(set = 1, binding = 0) uniform sampler2D meshTextures[64];

// Push constant for material data
layout(push_constant) uniform MaterialPushConstant {
    int albedoTexIndex;     // -1 = no texture, use base color
    int normalTexIndex;     // -1 = no normal map
    float metallic;
    float roughness;
} material;

// ========== Constants ==========

const float PI = 3.14159265359;
const vec3 SUN_DIRECTION = normalize(vec3(0.5, 1.0, 0.3));
const vec3 SUN_COLOR = vec3(1.0, 0.95, 0.8);
const vec3 AMBIENT_DAY = vec3(0.4, 0.45, 0.5);
const vec3 AMBIENT_NIGHT = vec3(0.05, 0.05, 0.1);

// ========== PBR Functions ==========

// Normal Distribution Function (GGX/Trowbridge-Reitz)
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return a2 / max(denom, 0.0001);
}

// Geometry Function (Schlick-GGX)
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float denom = NdotV * (1.0 - k) + k;

    return NdotV / max(denom, 0.0001);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx1 = GeometrySchlickGGX(NdotV, roughness);
    float ggx2 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

// Fresnel-Schlick approximation
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ========== Lighting Calculation ==========

vec3 calculatePBRLighting(vec3 albedo, float metallic, float roughness, vec3 normal, vec3 viewDir) {
    // Base reflectivity (dielectric = 0.04, metal = albedo)
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    // Calculate ambient based on time of day
    float dayFactor = smoothstep(0.2, 0.3, fragTimeOfDay) * (1.0 - smoothstep(0.7, 0.8, fragTimeOfDay));
    vec3 ambient = mix(AMBIENT_NIGHT, AMBIENT_DAY, dayFactor) * albedo;

    // Sun light direction (rotates with time of day)
    float sunAngle = fragTimeOfDay * 2.0 * PI;
    vec3 lightDir = normalize(vec3(sin(sunAngle) * 0.5, cos(sunAngle), 0.3));
    vec3 lightColor = SUN_COLOR * max(dot(lightDir, vec3(0, 1, 0)), 0.0);  // Fade when below horizon

    // Only calculate direct lighting if sun is above horizon
    vec3 Lo = vec3(0.0);
    if (dot(lightDir, vec3(0, 1, 0)) > 0.0) {
        vec3 H = normalize(viewDir + lightDir);

        // Cook-Torrance BRDF
        float NDF = DistributionGGX(normal, H, roughness);
        float G = GeometrySmith(normal, viewDir, lightDir, roughness);
        vec3 F = fresnelSchlick(max(dot(H, viewDir), 0.0), F0);

        vec3 numerator = NDF * G * F;
        float denominator = 4.0 * max(dot(normal, viewDir), 0.0) * max(dot(normal, lightDir), 0.0);
        vec3 specular = numerator / max(denominator, 0.0001);

        // Energy conservation
        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - metallic;  // Metals don't have diffuse

        // Calculate outgoing radiance
        float NdotL = max(dot(normal, lightDir), 0.0);
        Lo = (kD * albedo / PI + specular) * lightColor * NdotL;
    }

    // Final color
    vec3 color = ambient + Lo;

    return color;
}

// ========== Main ==========

void main() {
    // Sample albedo texture or use tint color
    vec4 albedoSample;
    if (material.albedoTexIndex >= 0 && material.albedoTexIndex < 64) {
        albedoSample = texture(meshTextures[material.albedoTexIndex], fragTexCoord);
    } else {
        albedoSample = vec4(1.0);  // White fallback
    }

    // Apply tint color
    vec3 albedo = albedoSample.rgb * fragTintColor.rgb;
    float alpha = albedoSample.a * fragTintColor.a;

    // Get normal from normal map or use vertex normal
    vec3 normal = normalize(fragNormal);

    if (material.normalTexIndex >= 0 && material.normalTexIndex < 64) {
        vec3 tangentNormal = texture(meshTextures[material.normalTexIndex], fragTexCoord).rgb;
        tangentNormal = tangentNormal * 2.0 - 1.0;

        // Build TBN matrix
        vec3 T = normalize(fragTangent);
        vec3 B = normalize(fragBitangent);
        vec3 N = normalize(fragNormal);
        mat3 TBN = mat3(T, B, N);

        normal = normalize(TBN * tangentNormal);
    }

    // View direction
    vec3 viewDir = normalize(camera.cameraPos.xyz - fragWorldPos);

    // PBR lighting calculation
    vec3 color = calculatePBRLighting(albedo, material.metallic, material.roughness, normal, viewDir);

    // Tone mapping (simple Reinhard)
    color = color / (color + vec3(1.0));

    // Gamma correction
    color = pow(color, vec3(1.0 / 2.2));

    // Output final color
    outColor = vec4(color, alpha);

    // Alpha cutoff for masked transparency
    if (outColor.a < 0.1) {
        discard;
    }
}
