#include <metal_stdlib>
using namespace metal;

#define GPU_MAX_SPHERES 32
#define GPU_MAX_PLANES  8
#define GPU_MAX_LIGHTS  8

struct GPUSurface {
    float cx, cy, cz, radius;
    float color_r, color_g, color_b, color_a;
    float reflectance, roughness, transparency, refractive_index;
};

struct GPULight {
    float px, py, pz, pw;
    float color_r, color_g, color_b, color_a;
};

struct GPUCamera {
    float lens_z, lens_r1, lens_r2, lens_radius;
    float lens_ri, cam_d, cam_z, has_lens;
};

struct GPUScene {
    int width, height;
    int min_samples, max_samples;
    float qual_thresh;
    int trace_depth;
    uint frame_seed;
    int num_spheres;
    int num_planes;
    int num_lights;
    float pad0, pad1;
    GPUCamera camera;
    GPUSurface spheres[GPU_MAX_SPHERES];
    GPUSurface planes[GPU_MAX_PLANES];
    GPULight lights[GPU_MAX_LIGHTS];
};

// --- RNG (PCG variant) ---

struct RNG {
    uint state;

    RNG(uint seed) {
        state = seed;
        // warm up
        next(); next();
    }

    uint next() {
        state = state * 747796405u + 2891336453u;
        uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        return (word >> 22u) ^ word;
    }

    float uniform() {
        return float(next()) / float(0xFFFFFFFFu);
    }
};

// --- Ray-sphere intersection (matches CPU geometric approach) ---

bool ray_sphere_intersection(float3 center, float radius,
                             float3 ray_origin, float3 ray_dir,
                             thread float3 &intersection) {
    float3 vpc = center - ray_origin;
    float vpc_len = length(vpc);

    // Project sphere center onto ray
    float tca = dot(vpc, ray_dir);
    float3 pc = ray_origin + tca * ray_dir;

    if (tca < 0) {
        // Sphere center is behind ray origin
        if (vpc_len > radius) return false;
        if (vpc_len <= radius + 0.00001) {
            float dist_pc_center = distance(pc, center);
            float half_chord = sqrt(radius * radius - dist_pc_center * dist_pc_center);
            float d = half_chord - distance(pc, ray_origin);
            intersection = ray_origin + d * ray_dir;
            return true;
        }
        return false;
    } else {
        float dist_pc_center = distance(pc, center);
        if (dist_pc_center > radius) return false;

        float half_chord = sqrt(radius * radius - dist_pc_center * dist_pc_center);
        float d;

        if ((vpc_len + 0.00001) >= radius) {
            d = distance(pc, ray_origin) - half_chord;
        } else {
            d = distance(pc, ray_origin) + half_chord;
        }

        intersection = ray_origin + d * ray_dir;
        return true;
    }
}

// --- Ray-plane intersection (axis-aligned planes) ---

bool ray_plane_intersection(constant GPUSurface &plane,
                            float3 ray_origin, float3 ray_dir,
                            thread float3 &intersection) {
    float3 normal = float3(plane.cx, plane.cy, plane.cz);
    float3 pos = normal * plane.radius; // radius field stores position

    if (dot(normal, ray_dir) >= 0) return false;

    float t;
    if (normal.x != 0)
        t = (pos.x - ray_origin.x) / ray_dir.x;
    else if (normal.y != 0)
        t = (pos.y - ray_origin.y) / ray_dir.y;
    else
        t = (pos.z - ray_origin.z) / ray_dir.z;

    if (t < 0.00001) return false;

    intersection = ray_origin + t * ray_dir;
    return true;
}

// --- Shadow test ---

bool line_of_sight(float3 a, float3 b,
                   constant GPUScene &scene) {
    float3 dir = normalize(b - a);

    for (int i = 0; i < scene.num_spheres; i++) {
        float3 center = float3(scene.spheres[i].cx,
                               scene.spheres[i].cy,
                               scene.spheres[i].cz);
        float r = scene.spheres[i].radius;
        float3 isect;
        if (ray_sphere_intersection(center, r, a, dir, isect)) {
            float da = distance(a, isect);
            float db = distance(b, isect);
            if (da > 0.001 && db > 0.001) {
                if (scene.spheres[i].transparency == 0)
                    return false;
            }
        }
    }

    for (int i = 0; i < scene.num_planes; i++) {
        float3 isect;
        if (ray_plane_intersection(scene.planes[i], a, dir, isect)) {
            float da = distance(a, isect);
            float db = distance(b, isect);
            if (da > 0.001 && db > 0.001) {
                if (scene.planes[i].transparency == 0)
                    return false;
            }
        }
    }

    return true;
}

// --- Lens refraction ---

bool ray_through_lens(float3 ray_origin, float3 ray_dir,
                      constant GPUCamera &cam,
                      thread float3 &out_origin, thread float3 &out_dir) {
    float z = cam.lens_z;
    float r1 = cam.lens_r1;
    float r2 = cam.lens_r2;
    float R = cam.lens_radius;
    float ri = cam.lens_ri;

    if (ray_origin.z < z) return false;
    if (ray_dir.z > 0) return false;

    // Front surface
    float3 front_center = float3(0, 0, z - sqrt(r1 * r1 - R * R));
    float3 isect;

    if (!ray_sphere_intersection(front_center, r1, ray_origin, ray_dir, isect))
        return false;

    // Check cap
    if (sqrt(isect.x * isect.x + isect.y * isect.y) > R)
        return false;

    // Refract into lens
    float3 normal = normalize(isect - front_center);
    float eta_in = 1.0 / ri;
    float3 refracted = refract(ray_dir, normal, eta_in);
    if (length(refracted) < 0.001) return false;

    out_origin = isect;
    out_dir = refracted;

    // Back surface
    float3 back_center = float3(0, 0, z + sqrt(r2 * r2 - R * R));

    if (!ray_sphere_intersection(back_center, r2, out_origin, out_dir, isect))
        return false;

    normal = normalize(isect - back_center);
    normal = -normal; // flip for exit surface

    float eta_out = ri / 1.0;
    refracted = refract(out_dir, normal, eta_out);
    if (length(refracted) < 0.001) return false;

    out_origin = isect;
    out_dir = refracted;

    return true;
}

// --- Perturb normal for roughness (matches CPU bias) ---

float3 perturb_normal(float3 n, float roughness, thread RNG &rng) {
    n.x += roughness * (1.0 - 0.5 * rng.uniform());
    n.y += roughness * (1.0 - 0.5 * rng.uniform());
    n.z += roughness * (1.0 - 0.5 * rng.uniform());
    return normalize(n);
}

// --- Iterative path tracer ---

float3 trace_ray(float3 origin, float3 direction,
                 constant GPUScene &scene, thread RNG &rng) {
    float3 accumulated = float3(0);
    float3 throughput = float3(1);

    float3 ray_origin = origin;
    float3 ray_dir = direction;
    float ray_ri = 1.0;

    for (int depth = 0; depth < scene.trace_depth; depth++) {
        // Find nearest intersection
        float nearest_dist = 1e30;
        int hit_type = -1; // 0=sphere, 1=plane
        int hit_idx = -1;
        float3 hit_point;

        for (int i = 0; i < scene.num_spheres; i++) {
            float3 center = float3(scene.spheres[i].cx,
                                   scene.spheres[i].cy,
                                   scene.spheres[i].cz);
            float r = scene.spheres[i].radius;
            float3 isect;
            if (ray_sphere_intersection(center, r, ray_origin, ray_dir, isect)) {
                float d = distance(isect, ray_origin);
                if (d > 0.00001 && d < nearest_dist) {
                    nearest_dist = d;
                    hit_point = isect;
                    hit_type = 0;
                    hit_idx = i;
                }
            }
        }

        for (int i = 0; i < scene.num_planes; i++) {
            float3 isect;
            if (ray_plane_intersection(scene.planes[i], ray_origin, ray_dir, isect)) {
                float d = distance(isect, ray_origin);
                if (d > 0.00001 && d < nearest_dist) {
                    nearest_dist = d;
                    hit_point = isect;
                    hit_type = 1;
                    hit_idx = i;
                }
            }
        }

        if (hit_type < 0) break;

        // Get surface properties and normal
        constant GPUSurface &surf = (hit_type == 0)
            ? scene.spheres[hit_idx]
            : scene.planes[hit_idx];

        float3 normal;
        if (hit_type == 0) {
            float3 center = float3(surf.cx, surf.cy, surf.cz);
            normal = normalize(hit_point - center);
        } else {
            normal = normalize(float3(surf.cx, surf.cy, surf.cz));
        }

        float4 color = float4(surf.color_r, surf.color_g, surf.color_b, surf.color_a);

        // Direct lighting
        for (int i = 0; i < scene.num_lights; i++) {
            float3 light_pos = float3(scene.lights[i].px,
                                      scene.lights[i].py,
                                      scene.lights[i].pz);
            float4 light_color = float4(scene.lights[i].color_r,
                                        scene.lights[i].color_g,
                                        scene.lights[i].color_b,
                                        scene.lights[i].color_a);

            if (!line_of_sight(hit_point, light_pos, scene)) continue;

            float3 incidence = light_pos - hit_point;
            float light_dist = length(incidence);
            incidence = normalize(incidence);

            float diffuse = dot(normal, incidence);
            if (diffuse < 0) diffuse = 0;

            // Reflect incidence about normal (same as CPU reflect_vector)
            float3 refl_dir = normalize(incidence - 2.0 * dot(incidence, normal) * normal);
            float specular = dot(refl_dir, ray_dir);
            if (specular < 0) specular = 0;

            float phong = (1.0 - surf.transparency) * diffuse * 0.9 +
                          pow(specular, 35.0) * surf.reflectance;

            accumulated += throughput * phong * color.rgb *
                          float3(light_color.r, light_color.g, light_color.b) /
                          (1.0 + light_dist * 0.0001);
        }

        // Choose next bounce: reflection or refraction
        float total_bounce = surf.reflectance + surf.transparency;
        if (total_bounce < 0.001) break;

        float3 rough_normal = normal;
        if (surf.roughness > 0)
            rough_normal = perturb_normal(normal, surf.roughness, rng);

        float r = rng.uniform();

        if (r < surf.reflectance / total_bounce) {
            // Reflection
            ray_dir = normalize(ray_dir - 2.0 * dot(ray_dir, rough_normal) * rough_normal);
            throughput *= total_bounce;
        } else {
            // Refraction
            float3 op = hit_point - ray_origin;
            float3 refracted;

            if (dot(op, rough_normal) < 0) {
                // Outside surface
                float eta = ray_ri / surf.refractive_index;
                refracted = refract(ray_dir, rough_normal, eta);
                if (length(refracted) > 0.001) {
                    ray_dir = refracted;
                    ray_ri = surf.refractive_index;
                } else {
                    ray_dir = normalize(ray_dir - 2.0 * dot(ray_dir, normal) * normal);
                }
            } else {
                // Inside surface
                float3 flipped = -rough_normal;
                float eta = ray_ri;
                refracted = refract(ray_dir, flipped, eta);
                if (length(refracted) > 0.001) {
                    ray_dir = refracted;
                    ray_ri = 1.0;
                } else {
                    ray_dir = normalize(ray_dir - 2.0 * dot(ray_dir, normal) * normal);
                }
            }
            throughput *= total_bounce;
        }

        ray_origin = hit_point;
    }

    return accumulated;
}

// --- Main kernel ---

kernel void trace_kernel(
    constant GPUScene &scene [[buffer(0)]],
    device float *output [[buffer(1)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int x = gid.x;
    int y = gid.y;
    int width = scene.width;
    int height = scene.height;

    if (x >= width || y >= height) return;

    uint pixel_idx = y * width + x;
    RNG rng(scene.frame_seed ^ pixel_idx * 1973u ^ uint(x) * 9277u ^ uint(y) * 6581u);

    float X = 2.0 * (0.5 - (float(x) / float(width)));
    float Y = 2.0 * (0.5 - (float(y) / float(height)));
    float aspect = float(width) / float(height);

    float3 sensor_origin = float3(
        scene.camera.cam_d * X * aspect,
        scene.camera.cam_d * Y,
        scene.camera.cam_z
    );

    float R = 0, G = 0, B = 0;
    float running_mean = 0;
    float last_check_mean = 0;
    int num_samples = 0;

    for (int s = 0; s < scene.max_samples; s++) {
        // DOF: sample point on lens
        float angle = 2.0 * M_PI_F * rng.uniform();
        float rad = 0.05 * rng.uniform();
        float3 lens_point = float3(
            rad * cos(angle),
            rad * sin(angle),
            scene.camera.lens_z
        );

        float3 ray_dir = normalize(lens_point - sensor_origin);
        float3 cam_origin, cam_dir;

        if (scene.camera.has_lens > 0.5) {
            if (!ray_through_lens(sensor_origin, ray_dir, scene.camera,
                                  cam_origin, cam_dir)) {
                continue;
            }
        } else {
            cam_origin = sensor_origin;
            cam_dir = ray_dir;
        }

        float3 color = trace_ray(cam_origin, cam_dir, scene, rng);

        // Cosine weighting (matches CPU sensor_normal = (0,0,-1))
        float cosine = 0.5 + 0.5 * (-cam_dir.z);
        color *= cosine;

        R += color.r;
        G += color.g;
        B += color.b;
        num_samples = s + 1;

        // Adaptive convergence
        float lum = color.r + color.g + color.b;
        if (s == 0) {
            running_mean = lum;
        } else {
            running_mean += (lum - running_mean) / float(s + 1);
        }

        if (s > scene.min_samples && (s % scene.min_samples == 0)) {
            float score = abs(running_mean - last_check_mean) / max(running_mean, 0.0001f);
            if (score < scene.qual_thresh) break;
            last_check_mean = running_mean;
        }
    }

    if (num_samples == 0) num_samples = 1;
    float inv_n = 1.0 / float(num_samples);
    int idx = pixel_idx * 3;
    output[idx + 0] = R * inv_n;
    output[idx + 1] = G * inv_n;
    output[idx + 2] = B * inv_n;
}
