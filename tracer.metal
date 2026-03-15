#include <metal_stdlib>
using namespace metal;

#define GPU_MAX_SPHERES  32
#define GPU_MAX_PLANES   8
#define GPU_MAX_LIGHTS   8
#define GPU_MAX_BVH_NODES 64
#define BVH_STACK_DEPTH  8
#define GPU_MAX_LENSES   8

struct GPUSurface {
    float cx, cy, cz, radius;
    float color_r, color_g, color_b, color_a;
    float reflectance, roughness, transparency, cauchy_a;
    float cauchy_b, emission, pad_s2, pad_s3;
};

struct GPULight {
    float px, py, pz, pw;
    float color_r, color_g, color_b, color_a;
};

struct GPULensElement {
    float z, r1, r2, radius;
    float cauchy_a, cauchy_b, reflectance, pad0;
};

struct GPUCamera {
    float cam_d, cam_z;
    int num_lenses, pad_cam;
    float aperture_z, aperture_radius, pad_a0, pad_a1;
    GPULensElement lenses[GPU_MAX_LENSES];
};

struct BVHNode {
    float min_x, min_y, min_z, pad0;
    float max_x, max_y, max_z, pad1;
    int left, right;
    int prim_start, prim_count;
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
    int num_bvh_nodes;
    int shadow_rays;
    int sample_offset;   // starting sample index for this batch
    int batch_size;      // number of samples this batch
    float pad0;
    float spec_norm_r, spec_norm_g, spec_norm_b, pad_sn;
    GPUCamera camera;
    GPUSurface spheres[GPU_MAX_SPHERES];
    GPUSurface planes[GPU_MAX_PLANES];
    GPULight lights[GPU_MAX_LIGHTS];
    BVHNode bvh[GPU_MAX_BVH_NODES];
    int bvh_prim_indices[GPU_MAX_SPHERES];
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

// --- Spectral helpers ---

float cauchy_ri(float A, float B, float wavelength_nm) {
    float lum = wavelength_nm / 1000.0;
    return A + B / (lum * lum);
}

float3 wavelength_to_rgb_raw(float wl) {
    float r = 0, g = 0, b = 0;

    if (wl >= 380 && wl < 440) {
        r = -(wl - 440.0) / (440.0 - 380.0);
        b = 1.0;
    } else if (wl < 490) {
        g = (wl - 440.0) / (490.0 - 440.0);
        b = 1.0;
    } else if (wl < 510) {
        g = 1.0;
        b = -(wl - 510.0) / (510.0 - 490.0);
    } else if (wl < 580) {
        r = (wl - 510.0) / (580.0 - 510.0);
        g = 1.0;
    } else if (wl < 645) {
        r = 1.0;
        g = -(wl - 645.0) / (645.0 - 580.0);
    } else if (wl <= 780) {
        r = 1.0;
    }

    float t;
    if (wl >= 380 && wl < 420)
        t = 0.3 + 0.7 * (wl - 380.0) / (420.0 - 380.0);
    else if (wl >= 700)
        t = 0.3 + 0.7 * (780.0 - wl) / (780.0 - 700.0);
    else
        t = 1.0;

    return float3(r * t, g * t, b * t);
}

float3 compute_spectral_norm() {
    float rs = 0, gs = 0, bs = 0;
    int N = 1000;
    for (int i = 0; i < N; i++) {
        float w = 380.0 + 400.0 * i / float(N);
        float3 c = wavelength_to_rgb_raw(w);
        rs += c.x; gs += c.y; bs += c.z;
    }
    return float3(float(N) / rs, float(N) / gs, float(N) / bs);
}

float3 wavelength_to_rgb(float wl, float3 norm) {
    return wavelength_to_rgb_raw(wl) * norm;
}

// --- Ray-sphere intersection (matches CPU geometric approach) ---

bool ray_sphere_intersection(float3 center, float radius,
                             float3 ray_origin, float3 ray_dir,
                             thread float3 &intersection) {
    float3 vpc = center - ray_origin;
    float vpc_len_sq = dot(vpc, vpc);
    float radius_sq = radius * radius;

    // Project sphere center onto ray
    float tca = dot(vpc, ray_dir);

    if (tca < 0) {
        // Sphere center is behind ray origin
        if (vpc_len_sq > radius_sq) return false;
        // We're inside the sphere - use squared distance to projected point
        float3 pc = ray_origin + tca * ray_dir;
        float3 pc_diff = pc - center;
        float dist_pc_center_sq = dot(pc_diff, pc_diff);
        float half_chord = sqrt(radius_sq - dist_pc_center_sq);
        // tca is negative, distance(pc, ray_origin) = -tca (since ray_dir is unit)
        float d = half_chord - (-tca);
        intersection = ray_origin + d * ray_dir;
        return true;
    } else {
        // tca >= 0, distance(pc, ray_origin) = tca
        float3 pc = ray_origin + tca * ray_dir;
        float3 pc_diff = pc - center;
        float dist_pc_center_sq = dot(pc_diff, pc_diff);
        if (dist_pc_center_sq > radius_sq) return false;

        float half_chord = sqrt(radius_sq - dist_pc_center_sq);
        float d;

        if (vpc_len_sq + 0.00001 >= radius_sq) {
            d = tca - half_chord;
        } else {
            d = tca + half_chord;
        }

        intersection = ray_origin + d * ray_dir;
        return true;
    }
}

// --- Ray-AABB intersection (slab method) ---

bool ray_aabb_intersection(float3 origin, float3 inv_dir,
                           float3 aabb_min, float3 aabb_max,
                           float max_dist) {
    float3 t1 = (aabb_min - origin) * inv_dir;
    float3 t2 = (aabb_max - origin) * inv_dir;
    float3 tmin = min(t1, t2);
    float3 tmax = max(t1, t2);
    float enter = max3(tmin.x, tmin.y, tmin.z);
    float exit_t = min3(tmax.x, tmax.y, tmax.z);
    return enter <= exit_t && exit_t > 0 && enter < max_dist;
}

// --- BVH traversal for sphere intersection ---

bool find_nearest_sphere(float3 ray_origin, float3 ray_dir,
                         constant GPUScene &scene,
                         thread float &nearest_dist,
                         thread float3 &hit_point,
                         thread int &hit_idx) {
    // Linear scan - with few spheres this is faster than BVH
    // and uses far less registers (no stack allocation)
    bool found = false;
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
                hit_idx = i;
                found = true;
            }
        }
    }
    return found;
}

// --- BVH traversal for shadow rays (any-hit) ---

bool any_hit_sphere(float3 a, float3 dir, float max_dist,
                    constant GPUScene &scene) {
    for (int i = 0; i < scene.num_spheres; i++) {
        float3 center = float3(scene.spheres[i].cx,
                               scene.spheres[i].cy,
                               scene.spheres[i].cz);
        float r = scene.spheres[i].radius;
        float3 isect;
        if (ray_sphere_intersection(center, r, a, dir, isect)) {
            float da = distance(a, isect);
            if (da > 0.001 && da < max_dist) {
                if (scene.spheres[i].transparency == 0)
                    return true;
            }
        }
    }
    return false;
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
    float max_dist = distance(a, b) - 0.001;

    // BVH-accelerated sphere test
    if (any_hit_sphere(a, dir, max_dist, scene))
        return false;

    // Planes (linear scan - typically 1-2)
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

// --- Fresnel reflectance (Schlick approximation) ---

float fresnel_schlick(float cosine, float n1, float n2) {
    float r0 = (n1 - n2) / (n1 + n2);
    r0 = r0 * r0;
    float c = 1.0 - cosine;
    return r0 + (1.0 - r0) * c * c * c * c * c;
}

// --- Multi-element lens system with Fresnel reflection ---
// Each lens element has two spherical surfaces (front and back).
// We trace sequentially through surfaces, with Fresnel reflection at each.
// Reflected rays can bounce between surfaces, creating lens flare ghosts.

#define LENS_MAX_BOUNCES 16
#define LENS_EPS 1e-5  // small epsilon for thin lens elements

// Find both intersection points with a sphere, return as t values along the ray.
// Returns count of valid (positive) intersections.
int ray_sphere_t(float3 center, float radius, float3 origin, float3 dir,
                 thread float &t0, thread float &t1) {
    float3 oc = origin - center;
    float b = dot(oc, dir);
    float c = dot(oc, oc) - radius * radius;
    float disc = b * b - c;
    if (disc < 0) return 0;
    float sq = sqrt(disc);
    t0 = -b - sq;
    t1 = -b + sq;
    // Return count of valid t values (epsilon to avoid self-intersection)
    int count = 0;
    if (t0 > LENS_EPS) count++;
    if (t1 > LENS_EPS) count++;
    return count;
}

bool ray_through_lens_system(float3 ray_origin, float3 ray_dir,
                             constant GPUCamera &cam, float wavelength,
                             thread float3 &out_origin, thread float3 &out_dir,
                             thread RNG &rng, thread bool &hit_dispersive) {
    if (cam.num_lenses <= 0) return false;

    float3 pos = ray_origin;
    float3 dir = ray_dir;
    float current_ri = 1.0;  // start in air

    // Compute valid z-range for the lens system to reject far-side sphere hits.
    // Any intersection z outside [z_min, z_max] is a spurious far-side hit.
    float z_front = cam.lenses[0].z;
    float z_back = cam.lenses[cam.num_lenses - 1].z;
    float z_margin = abs(z_front - z_back) + 0.5;  // generous margin
    float z_max = max(z_front, z_back) + z_margin;
    float z_min = min(z_front, z_back) - z_margin;

    for (int bounce = 0; bounce < LENS_MAX_BOUNCES; bounce++) {
        // Find nearest lens surface intersection across all elements
        float nearest_t = 1e30;
        float3 nearest_isect;
        float3 nearest_normal;  // always pointing outward from glass at surface
        int nearest_elem = -1;
        float nearest_ri = 1.0;
        bool nearest_entering = false;  // entering glass?

        for (int e = 0; e < cam.num_lenses; e++) {
            constant GPULensElement &elem = cam.lenses[e];
            float z = elem.z;
            float R = elem.radius;
            float ri = cauchy_ri(elem.cauchy_a, elem.cauchy_b, wavelength);

            // Front surface sphere
            float abs_r1 = abs(elem.r1);
            float sign1 = (elem.r1 > 0) ? 1.0 : -1.0;
            float3 fc = float3(0, 0, z - sign1 * sqrt(abs_r1 * abs_r1 - R * R));

            float t0, t1;
            int hits = ray_sphere_t(fc, abs_r1, pos, dir, t0, t1);
            // Check both intersections
            for (int h = 0; h < 2; h++) {
                float t = (h == 0) ? t0 : t1;
                if (t <= LENS_EPS || t >= nearest_t) continue;
                float3 isect = pos + t * dir;
                if (isect.z < z_min || isect.z > z_max) continue;  // reject far-side hits
                float xy_r = length(float2(isect.x, isect.y));
                if (xy_r > R) continue;

                // Surface normal: outward from sphere center
                float3 normal = normalize(isect - fc);
                bool entering = (dot(dir, normal) < 0);

                nearest_t = t;
                nearest_isect = isect;
                nearest_normal = normal;
                nearest_elem = e;
                nearest_ri = ri;
                nearest_entering = entering;
            }

            // Back surface sphere
            float abs_r2 = abs(elem.r2);
            float sign2 = (elem.r2 > 0) ? 1.0 : -1.0;
            float3 bc = float3(0, 0, z + sign2 * sqrt(abs_r2 * abs_r2 - R * R));

            hits = ray_sphere_t(bc, abs_r2, pos, dir, t0, t1);
            for (int h = 0; h < 2; h++) {
                float t = (h == 0) ? t0 : t1;
                if (t <= LENS_EPS || t >= nearest_t) continue;
                float3 isect = pos + t * dir;
                if (isect.z < z_min || isect.z > z_max) continue;  // reject far-side hits
                float xy_r = length(float2(isect.x, isect.y));
                if (xy_r > R) continue;

                float3 normal = normalize(isect - bc);
                bool entering = (dot(dir, normal) < 0);

                nearest_t = t;
                nearest_isect = isect;
                nearest_normal = normal;
                nearest_elem = e;
                nearest_ri = ri;
                nearest_entering = entering;
            }
        }

        // Check aperture: if ray crosses aperture plane before next surface, test radius
        if (cam.aperture_radius > 0 && abs(dir.z) > 0.0001) {
            float t_ap = (cam.aperture_z - pos.z) / dir.z;
            if (t_ap > LENS_EPS && (nearest_elem < 0 || t_ap < nearest_t)) {
                float3 ap_hit = pos + t_ap * dir;
                float ap_r = length(float2(ap_hit.x, ap_hit.y));
                if (ap_r > cam.aperture_radius)
                    return false;  // blocked by aperture
            }
        }

        if (nearest_elem < 0) {
            // No more surfaces hit — ray exits the lens system
            if (dir.z < 0) {
                out_origin = pos;
                out_dir = dir;
                return true;
            }
            return false;
        }

        if (cam.lenses[nearest_elem].cauchy_b > 0.0001)
            hit_dispersive = true;

        // Determine entering/exiting by current medium, not geometry
        // If we're in air (ri≈1), we must be entering glass; if in glass, exiting to air
        bool entering = (current_ri < 1.01);
        float n1 = current_ri;
        float n2 = entering ? nearest_ri : 1.0;

        // Fresnel + surface reflectance (e.g. coating)
        // AR coating factor: real multi-coated lenses reduce Fresnel to ~0.5% per surface
        float cos_i = abs(dot(dir, nearest_normal));
        float fresnel_r = fresnel_schlick(cos_i, n1, n2) * 0.1;  // AR coating: 90% reduction
        float surf_refl = cam.lenses[nearest_elem].reflectance;
        fresnel_r = fresnel_r + surf_refl * (1.0 - fresnel_r);  // blend: explicit reflectance on top

        // Surface normal for refraction: must point against incoming ray
        float3 n = (dot(dir, nearest_normal) < 0) ? nearest_normal : -nearest_normal;

        if (rng.uniform() < fresnel_r) {
            // Reflect off surface
            dir = normalize(dir - 2.0 * dot(dir, n) * n);
            pos = nearest_isect;
            // current_ri unchanged
        } else {
            // Refract through surface
            float eta = n1 / n2;
            float3 refracted = refract(dir, n, eta);
            if (length(refracted) < 0.001) {
                // Total internal reflection
                dir = normalize(dir - 2.0 * dot(dir, n) * n);
                pos = nearest_isect;
            } else {
                dir = refracted;
                pos = nearest_isect;
                current_ri = n2;
            }
        }
    }

    return false;
}

// --- Deterministic ghost ray through lens system ---
// Traces a ray through the lens, forcing reflection at exactly 2 surface hits.
// surface_hit_count tracks each surface encounter; when it equals reflect_at_1 or
// reflect_at_2, reflection is forced. All other surfaces refract normally.
// Returns the total weight (product of Fresnel reflectance at forced surfaces,
// and transmittance at others). Returns 0 if the ray is blocked or doesn't exit.

float ray_ghost_through_lens(float3 ray_origin, float3 ray_dir,
                              constant GPUCamera &cam, float wavelength,
                              int reflect_at_1, int reflect_at_2,
                              thread float3 &out_origin, thread float3 &out_dir) {
    if (cam.num_lenses <= 0) return 0;

    float3 pos = ray_origin;
    float3 dir = ray_dir;
    float current_ri = 1.0;
    float weight = 1.0;
    int surface_hit = 0;

    float z_front = cam.lenses[0].z;
    float z_back = cam.lenses[cam.num_lenses - 1].z;
    float z_margin = abs(z_front - z_back) + 0.5;
    float z_max = max(z_front, z_back) + z_margin;
    float z_min = min(z_front, z_back) - z_margin;

    for (int bounce = 0; bounce < LENS_MAX_BOUNCES; bounce++) {
        float nearest_t = 1e30;
        float3 nearest_isect;
        float3 nearest_normal;
        int nearest_elem = -1;
        float nearest_ri = 1.0;

        for (int e = 0; e < cam.num_lenses; e++) {
            constant GPULensElement &elem = cam.lenses[e];
            float z = elem.z;
            float R = elem.radius;
            float ri = cauchy_ri(elem.cauchy_a, elem.cauchy_b, wavelength);

            // Front surface
            float abs_r1 = abs(elem.r1);
            float sign1 = (elem.r1 > 0) ? 1.0 : -1.0;
            float3 fc = float3(0, 0, z - sign1 * sqrt(abs_r1 * abs_r1 - R * R));
            float t0, t1;
            ray_sphere_t(fc, abs_r1, pos, dir, t0, t1);
            for (int h = 0; h < 2; h++) {
                float t = (h == 0) ? t0 : t1;
                if (t <= LENS_EPS || t >= nearest_t) continue;
                float3 isect = pos + t * dir;
                if (isect.z < z_min || isect.z > z_max) continue;
                if (length(float2(isect.x, isect.y)) > R) continue;
                float3 normal = normalize(isect - fc);
                nearest_t = t; nearest_isect = isect; nearest_normal = normal;
                nearest_elem = e; nearest_ri = ri;
            }

            // Back surface
            float abs_r2 = abs(elem.r2);
            float sign2 = (elem.r2 > 0) ? 1.0 : -1.0;
            float3 bc = float3(0, 0, z + sign2 * sqrt(abs_r2 * abs_r2 - R * R));
            ray_sphere_t(bc, abs_r2, pos, dir, t0, t1);
            for (int h = 0; h < 2; h++) {
                float t = (h == 0) ? t0 : t1;
                if (t <= LENS_EPS || t >= nearest_t) continue;
                float3 isect = pos + t * dir;
                if (isect.z < z_min || isect.z > z_max) continue;
                if (length(float2(isect.x, isect.y)) > R) continue;
                float3 normal = normalize(isect - bc);
                nearest_t = t; nearest_isect = isect; nearest_normal = normal;
                nearest_elem = e; nearest_ri = ri;
            }
        }

        // Aperture check
        if (cam.aperture_radius > 0 && abs(dir.z) > 0.0001) {
            float t_ap = (cam.aperture_z - pos.z) / dir.z;
            if (t_ap > LENS_EPS && (nearest_elem < 0 || t_ap < nearest_t)) {
                float3 ap_hit = pos + t_ap * dir;
                if (length(float2(ap_hit.x, ap_hit.y)) > cam.aperture_radius)
                    return 0;  // blocked
            }
        }

        if (nearest_elem < 0) {
            if (dir.z < 0) {
                out_origin = pos;
                out_dir = dir;
                return weight;
            }
            return 0;
        }

        bool entering = (current_ri < 1.01);
        float n1 = current_ri;
        float n2 = entering ? nearest_ri : 1.0;
        float cos_i = abs(dot(dir, nearest_normal));
        float fresnel_r = fresnel_schlick(cos_i, n1, n2);
        float surf_refl = cam.lenses[nearest_elem].reflectance;
        fresnel_r = fresnel_r + surf_refl * (1.0 - fresnel_r);

        float3 n = (dot(dir, nearest_normal) < 0) ? nearest_normal : -nearest_normal;

        bool force_reflect = (surface_hit == reflect_at_1 || surface_hit == reflect_at_2);
        surface_hit++;

        if (force_reflect) {
            // Force reflection, weight by reflectance
            weight *= fresnel_r;
            dir = normalize(dir - 2.0 * dot(dir, n) * n);
            pos = nearest_isect;
        } else {
            // Force refraction, weight by transmittance
            weight *= (1.0 - fresnel_r);
            float eta = n1 / n2;
            float3 refracted = refract(dir, n, eta);
            if (length(refracted) < 0.001) return 0;  // TIR kills this ghost path
            dir = refracted;
            pos = nearest_isect;
            current_ri = n2;
        }
    }

    return 0;
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
                 constant GPUScene &scene, thread RNG &rng, float wavelength,
                 thread int &out_bounces, thread bool &hit_dispersive) {
    float3 accumulated = float3(0);
    float3 throughput = float3(1);
    // Don't reset hit_dispersive — caller may have set it (e.g. from lens system)

    float3 ray_origin = origin;
    float3 ray_dir = direction;
    float ray_ri = 1.0;

    for (int depth = 0; depth < scene.trace_depth; depth++) {
        // Find nearest intersection
        float nearest_dist = 1e30;
        int hit_type = -1; // 0=sphere, 1=plane
        int hit_idx = -1;
        float3 hit_point;

        // BVH-accelerated sphere intersection
        {
            float3 sp;
            int si;
            if (find_nearest_sphere(ray_origin, ray_dir, scene,
                                        nearest_dist, sp, si)) {
                hit_point = sp;
                hit_type = 0;
                hit_idx = si;
            }
        }

        // Planes (linear scan - typically 1-2)
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

        // Emission: emissive surfaces contribute light directly
        if (surf.emission > 0) {
            accumulated += throughput * surf.emission * color.rgb;
        }

        // Direct lighting - skip when max contribution is negligible
        float max_phong = (1.0 - surf.transparency) * 0.9 + surf.reflectance;
        if (max_phong > 0.01) {
            for (int i = 0; i < scene.num_lights; i++) {
                float3 light_pos = float3(scene.lights[i].px,
                                          scene.lights[i].py,
                                          scene.lights[i].pz);
                float4 light_color = float4(scene.lights[i].color_r,
                                            scene.lights[i].color_g,
                                            scene.lights[i].color_b,
                                            scene.lights[i].color_a);

                // Shadow ray visibility test (optional)
                if (scene.shadow_rays) {
                    if (!line_of_sight(hit_point, light_pos, scene)) continue;
                }

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
        }

        // Choose next bounce: reflection or refraction
        float total_bounce = surf.reflectance + surf.transparency;
        if (total_bounce < 0.001) break;

        // Russian roulette: unbiased early termination for low-throughput paths
        // Start after typical glass traversal depth to preserve caustic quality
        if (depth > 16) {
            float max_t = max(throughput.r, max(throughput.g, throughput.b));
            float survive = min(max_t, 0.95f);
            if (rng.uniform() > survive) break;
            throughput /= survive;
        }

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
                float surf_ri = cauchy_ri(surf.cauchy_a, surf.cauchy_b, wavelength);
                if (surf.cauchy_b > 0.0001) hit_dispersive = true;
                float eta = ray_ri / surf_ri;
                refracted = refract(ray_dir, rough_normal, eta);
                if (length(refracted) > 0.001) {
                    ray_dir = refracted;
                    ray_ri = surf_ri;
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
        out_bounces = depth + 1;
    }

    return accumulated;
}

// --- Main kernel ---

// Accumulation buffer layout per pixel: [R_sum, G_sum, B_sum, num_samples, running_mean, M2]
#define ACCUM_STRIDE 6

kernel void trace_kernel(
    constant GPUScene &scene [[buffer(0)]],
    device float *output [[buffer(1)]],
    device float *accum [[buffer(2)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int x = gid.x;
    int y = gid.y;
    int width = scene.width;
    int height = scene.height;

    if (x >= width || y >= height) return;

    uint pixel_idx = y * width + x;

    // Load accumulated state
    int aidx = pixel_idx * ACCUM_STRIDE;
    float R = accum[aidx + 0];
    float G = accum[aidx + 1];
    float B = accum[aidx + 2];
    int prev_samples = int(accum[aidx + 3]);
    float running_mean = accum[aidx + 4];
    float M2 = accum[aidx + 5]; // sum of squared differences (Welford)

    // Already converged from previous batch?
    if (prev_samples >= scene.max_samples) return;
    if (prev_samples >= scene.min_samples) {
        // Standard error of the mean (noise in the averaged pixel value)
        float variance = M2 / float(prev_samples);
        float std_error = sqrt(variance / float(prev_samples));
        float rel_error = std_error / max(running_mean, 0.0001f);
        if (rel_error < scene.qual_thresh) return;
    }

    // Fixed per-pixel RNG for Cranley-Patterson rotation offsets (stable across batches)
    RNG pixel_rng(scene.frame_seed ^ pixel_idx * 1973u ^ uint(x) * 9277u ^ uint(y) * 6581u);
    float pixel_rand_w = pixel_rng.uniform();  // wavelength offset
    float pixel_rand_a = pixel_rng.uniform();  // aperture angle offset
    float pixel_rand_b = pixel_rng.uniform();  // aperture radius offset

    // Seed RNG incorporating sample_offset so each batch gets different random state
    RNG rng(scene.frame_seed ^ pixel_idx * 1973u ^ uint(x) * 9277u ^ uint(y) * 6581u
            ^ uint(scene.sample_offset) * 104729u);

    float X = 2.0 * (0.5 - (float(x) / float(width)));
    float Y = 2.0 * (0.5 - (float(y) / float(height)));
    float aspect = float(width) / float(height);

    float3 sensor_origin = float3(
        scene.camera.cam_d * X * aspect,
        scene.camera.cam_d * Y,
        scene.camera.cam_z
    );

    float3 spec_norm = float3(scene.spec_norm_r, scene.spec_norm_g, scene.spec_norm_b);

    int batch_count = min(scene.batch_size, scene.max_samples - prev_samples);
    if (batch_count <= 0) return;
    int num_samples = prev_samples;

    for (int i = 0; i < batch_count; i++) {
        // Use seq_idx for low-discrepancy sequences — must advance even when rays are blocked
        int seq_idx = prev_samples + i;

        // Low-discrepancy wavelength sampling (golden ratio + Cranley-Patterson rotation)
        float wavelength = 380.0 + 400.0 * fract(float(seq_idx) * 0.6180339887 + pixel_rand_w);

        float3 cam_origin, cam_dir;
        bool dispersive = false;

        if (scene.camera.num_lenses > 0) {
            // DOF: stratified lens sampling — use aperture radius if defined, else front element
            constant GPULensElement &front = scene.camera.lenses[0];
            float dof_radius = front.radius;
            if (scene.camera.aperture_radius > 0)
                dof_radius = min(dof_radius, scene.camera.aperture_radius);
            float angle = 2.0 * M_PI_F * fract(float(seq_idx) * 0.7548776662 + pixel_rand_a);
            float rad = dof_radius * sqrt(fract(float(seq_idx) * 0.3247179572 + pixel_rand_b));
            float3 lens_point = float3(
                rad * cos(angle),
                rad * sin(angle),
                front.z
            );

            float3 ray_dir = normalize(lens_point - sensor_origin);

            if (!ray_through_lens_system(sensor_origin, ray_dir, scene.camera,
                                         wavelength, cam_origin, cam_dir,
                                         rng, dispersive)) {
                continue;
            }
        } else {
            cam_origin = sensor_origin;
            cam_dir = normalize(float3(0, 0, 0) - sensor_origin);
        }

        int bounces = 0;
        float3 color = trace_ray(cam_origin, cam_dir, scene, rng, wavelength,
                                 bounces, dispersive);

        // Spectral weighting: only apply when ray hit dispersive material
        // Non-dispersive paths use white light to avoid colored noise
        if (dispersive)
            color *= wavelength_to_rgb(wavelength, spec_norm);

        // Cosine weighting
        float cosine = 0.5 + 0.5 * (-cam_dir.z);
        color *= cosine;

        // Clamp fireflies: cap per-sample luminance to prevent spectral outliers
        float lum_check = color.r + color.g + color.b;
        if (lum_check > 30.0) color *= 30.0 / lum_check;

        R += color.r;
        G += color.g;
        B += color.b;
        num_samples++;

        // Adaptive convergence (Welford's online variance)
        float lum = color.r + color.g + color.b;
        float old_mean = running_mean;
        if (num_samples == 1) {
            running_mean = lum;
            M2 = 0;
        } else {
            running_mean += (lum - running_mean) / float(num_samples);
            M2 += (lum - old_mean) * (lum - running_mean);
        }

        if (num_samples >= scene.min_samples && (num_samples % 16 == 0)) {
            float variance = M2 / float(num_samples);
            float std_error = sqrt(variance / float(num_samples));
            float rel_error = std_error / max(running_mean, 0.0001f);
            if (rel_error < scene.qual_thresh) break;
        }
    }

    // Save accumulated state
    accum[aidx + 0] = R;
    accum[aidx + 1] = G;
    accum[aidx + 2] = B;
    accum[aidx + 3] = float(num_samples);
    accum[aidx + 4] = running_mean;
    accum[aidx + 5] = M2;

    // Write current best output (averaged)
    int n = max(num_samples, 1);
    float inv_n = 1.0 / float(n);
    int idx = pixel_idx * 3;
    output[idx + 0] = R * inv_n;
    output[idx + 1] = G * inv_n;
    output[idx + 2] = B * inv_n;
}
