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
    float cauchy_b, emission, phong, is_lens;
};

struct GPULight {
    float px, py, pz, pw;
    float color_r, color_g, color_b, color_a;
    float specular, diffuse_mult, pad_l2, pad_l3;
};

struct GPULensElement {
    float z, r1, r2, radius;
    float cauchy_a, cauchy_b, reflectance, anamorphic;
    float front_center_z, back_center_z;
    float pad_le0, pad_le1;
};

struct GPUCamera {
    float cam_d, cam_z;
    int num_lenses, pad_cam;
    float aperture_z, aperture_radius, pad_a0, pad_a1;
    GPULensElement lenses[GPU_MAX_LENSES];
    int ghost_pairs[90 * 2];
    int num_ghost_pairs;
    int pad_gp;
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
    int ghost_rays;
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

#define LENS_MAX_BOUNCES 32
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

// Find intersection with a cylinder whose axis is in the x-y plane at angle `axis_angle` (degrees).
// axis_angle=90 => axis along y (curved in x-z), axis_angle=180 => axis along -x (curved in y-z), etc.
// center_z = z position of the cylinder center on the optical axis.
// The cylinder is infinite along the axis direction, curved perpendicular to it.
int ray_cylinder_t(float center_z, float radius, float axis_angle_deg,
                   float3 origin, float3 dir,
                   thread float &t0, thread float &t1) {
    // Cylinder axis direction in x-y plane
    float theta = axis_angle_deg * (M_PI_F / 180.0);
    float2 axis = float2(cos(theta), sin(theta));
    // Perpendicular direction in x-y plane
    float2 perp = float2(-axis.y, axis.x);

    // Project ray origin and direction onto (perp, z) plane
    float3 oc = origin - float3(0, 0, center_z);
    float oc_p = oc.x * perp.x + oc.y * perp.y;  // perp component
    float oc_z = oc.z;                              // z component
    float dir_p = dir.x * perp.x + dir.y * perp.y;
    float dir_z = dir.z;

    // 2D circle intersection in (perp, z) space
    float a = dir_p * dir_p + dir_z * dir_z;
    if (a < 1e-10) return 0;
    float b = oc_p * dir_p + oc_z * dir_z;
    float c = oc_p * oc_p + oc_z * oc_z - radius * radius;
    float disc = b * b - a * c;
    if (disc < 0) return 0;
    float sq = sqrt(disc);
    float inv_a = 1.0 / a;
    t0 = (-b - sq) * inv_a;
    t1 = (-b + sq) * inv_a;
    int count = 0;
    if (t0 > LENS_EPS) count++;
    if (t1 > LENS_EPS) count++;
    return count;
}

// Cylinder surface normal at intersection point
float3 cylinder_normal(float3 isect, float center_z, float axis_angle_deg) {
    float theta = axis_angle_deg * (M_PI_F / 180.0);
    float2 axis = float2(cos(theta), sin(theta));
    // Project intersection onto axis to find closest point on cylinder axis
    float3 oc = isect - float3(0, 0, center_z);
    float along_axis = oc.x * axis.x + oc.y * axis.y;
    float3 axis_point = float3(along_axis * axis.x, along_axis * axis.y, 0);
    return normalize(oc - axis_point);
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
            float R_sq = elem.radius * elem.radius;
            float ri = cauchy_ri(elem.cauchy_a, elem.cauchy_b, wavelength);

            // Front surface
            float abs_r1 = abs(elem.r1);
            float fc_z = elem.front_center_z;
            bool is_cyl = (elem.anamorphic > 0.5);
            float cyl_angle = elem.anamorphic;

            float t0, t1;
            if (is_cyl) {
                ray_cylinder_t(fc_z, abs_r1, cyl_angle, pos, dir, t0, t1);
            } else {
                ray_sphere_t(float3(0, 0, fc_z), abs_r1, pos, dir, t0, t1);
            }
            // Check both intersections
            for (int h = 0; h < 2; h++) {
                float t = (h == 0) ? t0 : t1;
                if (t <= LENS_EPS || t >= nearest_t) continue;
                float3 isect = pos + t * dir;
                if (isect.z < z_min || isect.z > z_max) continue;
                if (isect.x * isect.x + isect.y * isect.y > R_sq) continue;

                float3 normal = is_cyl
                    ? cylinder_normal(isect, fc_z, cyl_angle)
                    : normalize(isect - float3(0, 0, fc_z));
                bool entering = (dot(dir, normal) < 0);

                nearest_t = t;
                nearest_isect = isect;
                nearest_normal = normal;
                nearest_elem = e;
                nearest_ri = ri;
                nearest_entering = entering;
            }

            // Back surface
            float abs_r2 = abs(elem.r2);
            float bc_z = elem.back_center_z;

            if (is_cyl) {
                ray_cylinder_t(bc_z, abs_r2, cyl_angle, pos, dir, t0, t1);
            } else {
                ray_sphere_t(float3(0, 0, bc_z), abs_r2, pos, dir, t0, t1);
            }
            for (int h = 0; h < 2; h++) {
                float t = (h == 0) ? t0 : t1;
                if (t <= LENS_EPS || t >= nearest_t) continue;
                float3 isect = pos + t * dir;
                if (isect.z < z_min || isect.z > z_max) continue;
                if (isect.x * isect.x + isect.y * isect.y > R_sq) continue;

                float3 normal = is_cyl
                    ? cylinder_normal(isect, bc_z, cyl_angle)
                    : normalize(isect - float3(0, 0, bc_z));
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
                if (ap_hit.x * ap_hit.x + ap_hit.y * ap_hit.y > cam.aperture_radius * cam.aperture_radius)
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

        if (cam.lenses[nearest_elem].cauchy_b > 0.005)
            hit_dispersive = true;

        // Determine entering/exiting by current medium, not geometry
        // If we're in air (ri≈1), we must be entering glass; if in glass, exiting to air
        bool entering = (current_ri < 1.01);
        float n1 = current_ri;
        float n2 = entering ? nearest_ri : 1.0;

        // Fresnel + surface reflectance
        float cos_i = abs(dot(dir, nearest_normal));
        float fresnel_r = fresnel_schlick(cos_i, n1, n2);
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
// Traces a ray through the lens, forcing reflection at exactly 2 physical surfaces.
// Surface IDs: elem_index * 2 + side (0=front, 1=back).
// reflect_surf_1 should be the DEEPER surface (higher z-depth, hit first on forward pass).
// reflect_surf_2 should be the SHALLOWER surface (hit on the backward bounce).
// Returns weight (product of reflectances/transmittances). 0 = path failed.

float ray_ghost_through_lens(float3 ray_origin, float3 ray_dir,
                              constant GPUCamera &cam, float wavelength,
                              int reflect_surf_1, int reflect_surf_2,
                              thread float3 &out_origin, thread float3 &out_dir) {
    if (cam.num_lenses <= 0) return 0;

    float3 pos = ray_origin;
    float3 dir = ray_dir;
    float current_ri = 1.0;
    float weight = 1.0;
    int reflections_done = 0;

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
        int nearest_surf_id = -1;  // physical surface ID
        float nearest_ri = 1.0;

        for (int e = 0; e < cam.num_lenses; e++) {
            constant GPULensElement &elem = cam.lenses[e];
            float R_sq = elem.radius * elem.radius;
            float ri = cauchy_ri(elem.cauchy_a, elem.cauchy_b, wavelength);
            bool is_cyl = (elem.anamorphic > 0.5);
            float cyl_ang = elem.anamorphic;

            // Front surface (surf_id = e*2)
            float abs_r1 = abs(elem.r1);
            float fc_z = elem.front_center_z;
            float t0, t1;
            if (is_cyl) ray_cylinder_t(fc_z, abs_r1, cyl_ang, pos, dir, t0, t1);
            else ray_sphere_t(float3(0, 0, fc_z), abs_r1, pos, dir, t0, t1);
            for (int h = 0; h < 2; h++) {
                float t = (h == 0) ? t0 : t1;
                if (t <= LENS_EPS || t >= nearest_t) continue;
                float3 isect = pos + t * dir;
                if (isect.z < z_min || isect.z > z_max) continue;
                if (isect.x * isect.x + isect.y * isect.y > R_sq) continue;
                float3 normal = is_cyl ? cylinder_normal(isect, fc_z, cyl_ang)
                                       : normalize(isect - float3(0, 0, fc_z));
                nearest_t = t; nearest_isect = isect; nearest_normal = normal;
                nearest_elem = e; nearest_ri = ri; nearest_surf_id = e * 2;
            }

            // Back surface (surf_id = e*2 + 1)
            float abs_r2 = abs(elem.r2);
            float bc_z = elem.back_center_z;
            if (is_cyl) ray_cylinder_t(bc_z, abs_r2, cyl_ang, pos, dir, t0, t1);
            else ray_sphere_t(float3(0, 0, bc_z), abs_r2, pos, dir, t0, t1);
            for (int h = 0; h < 2; h++) {
                float t = (h == 0) ? t0 : t1;
                if (t <= LENS_EPS || t >= nearest_t) continue;
                float3 isect = pos + t * dir;
                if (isect.z < z_min || isect.z > z_max) continue;
                if (isect.x * isect.x + isect.y * isect.y > R_sq) continue;
                float3 normal = is_cyl ? cylinder_normal(isect, bc_z, cyl_ang)
                                       : normalize(isect - float3(0, 0, bc_z));
                nearest_t = t; nearest_isect = isect; nearest_normal = normal;
                nearest_elem = e; nearest_ri = ri; nearest_surf_id = e * 2 + 1;
            }
        }

        // Aperture check: use larger ghost aperture for anamorphic streak extent
        float ghost_stop = max(cam.aperture_radius * 8.0f, 0.05f);
        if (ghost_stop > 0 && abs(dir.z) > 0.0001) {
            float t_ap = (cam.aperture_z - pos.z) / dir.z;
            if (t_ap > LENS_EPS && (nearest_elem < 0 || t_ap < nearest_t)) {
                float3 ap_hit = pos + t_ap * dir;
                if (ap_hit.x * ap_hit.x + ap_hit.y * ap_hit.y > ghost_stop * ghost_stop)
                    return 0;
            }
        }

        if (nearest_elem < 0) {
            if (dir.z < 0 && reflections_done == 2) {
                out_origin = pos;
                out_dir = dir;
                return weight;
            }
            return 0;  // exited without completing both reflections
        }

        bool entering = (current_ri < 1.01);
        float n1 = current_ri;
        float n2 = entering ? nearest_ri : 1.0;
        float cos_i = abs(dot(dir, nearest_normal));
        float fresnel_r = fresnel_schlick(cos_i, n1, n2);
        float surf_refl = cam.lenses[nearest_elem].reflectance;
        fresnel_r = fresnel_r + surf_refl * (1.0 - fresnel_r);

        float3 n = (dot(dir, nearest_normal) < 0) ? nearest_normal : -nearest_normal;

        // Force reflection at target surfaces (only if we haven't already reflected there)
        bool force_reflect = false;
        if (reflections_done == 0 && nearest_surf_id == reflect_surf_1)
            force_reflect = true;
        else if (reflections_done == 1 && nearest_surf_id == reflect_surf_2)
            force_reflect = true;

        if (force_reflect) {
            weight *= fresnel_r;
            if (weight <= 1e-7f) return 0;
            dir = normalize(dir - 2.0 * dot(dir, n) * n);
            pos = nearest_isect;
            reflections_done++;
        } else {
            weight *= (1.0 - fresnel_r);
            if (weight <= 1e-7f) return 0;
            float eta = n1 / n2;
            float3 refracted = refract(dir, n, eta);
            if (length(refracted) < 0.001) return 0;
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
                 thread int &out_bounces, thread bool &hit_dispersive,
                 thread float3 &out_glint) {
    float3 accumulated = float3(0);
    out_glint = float3(0);
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

                // Fresnel specular: use canonical view direction (wavelength-independent)
                float3 canonical_view = normalize(hit_point - float3(0, 0, scene.camera.cam_z));
                float3 refl_view = canonical_view - 2.0 * dot(canonical_view, normal) * normal;
                float3 to_light = normalize(light_pos - hit_point);
                float refl_cos = dot(refl_view, to_light);

                // Schlick Fresnel: F0 from refractive index
                float n_ior = surf.cauchy_a;
                float f0 = ((n_ior - 1.0) / (n_ior + 1.0)) * ((n_ior - 1.0) / (n_ior + 1.0));
                float cos_i = max(dot(normal, to_light), 0.0f);
                float fresnel = f0 + (1.0 - f0) * pow(1.0 - cos_i, 5.0);

                // Glint: tight cone around perfect reflection
                float glint_size = 1.0 / surf.phong;
                float spec_mult = scene.lights[i].specular;
                float spec_term = 0.0;
                // Don't apply boosted specular to lens elements
                float effective_mult = (surf.is_lens < 0.5) ? spec_mult : 1.0;
                if (refl_cos > (1.0 - glint_size)) {
                    float t = (refl_cos - (1.0 - glint_size)) / glint_size;
                    spec_term = t * t * fresnel * effective_mult;
                }

                float diff_term = (1.0 - surf.transparency) * diffuse * 0.9 * scene.lights[i].diffuse_mult;

                float3 lc = float3(light_color.r, light_color.g, light_color.b);
                float atten = 1.0 / (1.0 + light_dist * 0.0001);
                accumulated += throughput * diff_term * color.rgb * lc * atten;
                // Specular glints bypass wavelength weighting — always white
                out_glint += spec_term * lc * atten;
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
                if (surf.cauchy_b > 0.005) hit_dispersive = true;
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

// Accumulation buffer layout per pixel:
// [R_sum, G_sum, B_sum, num_samples, running_mean, M2, ghost_R, ghost_G, ghost_B, ghost_n]
#define ACCUM_STRIDE 10

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
        float3 lens_point = float3(0, 0, 0);
        bool dispersive = false;

        if (scene.camera.num_lenses > 0) {
            // DOF: stratified lens sampling
            constant GPULensElement &front = scene.camera.lenses[0];
            float dof_radius = front.radius;
            for (int le = 1; le < scene.camera.num_lenses; le++)
                dof_radius = min(dof_radius, scene.camera.lenses[le].radius);
            if (scene.camera.aperture_radius > 0)
                dof_radius = min(dof_radius, scene.camera.aperture_radius);
            float angle = 2.0 * M_PI_F * fract(float(seq_idx) * 0.7548776662 + pixel_rand_a);
            float rad = dof_radius * sqrt(fract(float(seq_idx) * 0.3247179572 + pixel_rand_b));
            lens_point = float3(
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
        float3 glint = float3(0);
        float3 color = trace_ray(cam_origin, cam_dir, scene, rng, wavelength,
                                 bounces, dispersive, glint);

        // Spectral weighting: only apply when ray hit dispersive material
        // Non-dispersive paths use white light to avoid colored noise
        if (dispersive)
            color *= wavelength_to_rgb(wavelength, spec_norm);

        // Cosine weighting
        float cosine = 0.5 + 0.5 * (-cam_dir.z);
        color *= cosine;

        // Add glint before firefly clamp so bright specular spikes get compressed
        color += glint;

        // Soft firefly clamp: smoothly compress bright outliers
        float lum_check = color.r + color.g + color.b;
        float clamp_knee = 12.0;
        if (lum_check > clamp_knee) {
            float excess = lum_check - clamp_knee;
            float compressed = clamp_knee + clamp_knee * excess / (clamp_knee + excess);
            color *= compressed / lum_check;
        }

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

    // Output: main image only (ghosts composited by separate kernel)
    int n = max(num_samples, 1);
    float inv_n = 1.0 / float(n);

    int idx = pixel_idx * 3;
    output[idx + 0] = R * inv_n;
    output[idx + 1] = G * inv_n;
    output[idx + 2] = B * inv_n;
}

// --- Ghost kernel: real ray tracing through the lens system ---
// Traces ghost rays from each pixel through the actual lens geometry
// using ray_ghost_through_lens (forced 2-bounce reflections).
// The anamorphic cylinder element naturally produces stretched ghost images.
// Ghost rays that exit the lens intersect the scene to find emissive sources
// and specular glints on glass surfaces.

#define GHOST_SAMPLES 256

kernel void ghost_kernel(
    constant GPUScene &scene [[buffer(0)]],
    device float *output [[buffer(1)]],
    device float *ghost_buf [[buffer(2)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int x = gid.x;
    int y = gid.y;
    int width = scene.width;
    int height = scene.height;

    if (x >= width || y >= height) return;
    if (!scene.ghost_rays || scene.camera.num_ghost_pairs <= 0) return;

    uint pixel_idx = y * width + x;

    float X = 2.0 * (0.5 - (float(x) / float(width)));
    float Y = 2.0 * (0.5 - (float(y) / float(height)));
    float aspect = float(width) / float(height);

    float3 sensor_origin = float3(
        scene.camera.cam_d * X * aspect,
        scene.camera.cam_d * Y,
        scene.camera.cam_z
    );

    float3 spec_norm = float3(scene.spec_norm_r, scene.spec_norm_g, scene.spec_norm_b);

    // Ghost pupil: use a LARGER pupil than the beauty aperture to get
    // long anamorphic streaks. The flare extent is proportional to pupil size.
    // Use the front lens element radius (not the small beauty aperture).
    float dof_radius = scene.camera.lenses[0].radius;
    for (int le = 1; le < scene.camera.num_lenses; le++)
        dof_radius = min(dof_radius, scene.camera.lenses[le].radius);
    // Use full lens clear aperture for ghost paths — larger pupil = longer streaks
    float ghost_pupil = dof_radius;

    // Per-pixel RNG for Cranley-Patterson rotation offsets
    RNG pixel_rng(scene.frame_seed ^ pixel_idx * 1973u ^ uint(x) * 9277u ^ uint(y) * 6581u);
    float pixel_rand_w = pixel_rng.uniform();
    float pixel_rand_a = pixel_rng.uniform();
    float pixel_rand_r = pixel_rng.uniform();

    // Sample RNG (separate from pixel_rng to vary across samples)
    RNG rng(scene.frame_seed ^ pixel_idx * 2971u ^ uint(x) * 7919u ^ uint(y) * 4391u ^ 0x60570u);

    float ghost_R = 0, ghost_G = 0, ghost_B = 0;
    float debug_exits = 0;
    float debug_hits = 0;
    float debug_emissive_hits = 0;
    float debug_glint_hits = 0;
    float debug_max_gw = 0;

    float front_z = scene.camera.lenses[0].z;

    for (int i = 0; i < GHOST_SAMPLES; i++) {
        float wavelength = 380.0 + 400.0 * fract(float(i) * 0.6180339887 + pixel_rand_w);

        // Sample entrance pupil point (same stratification as trace_kernel)
        float angle = 2.0 * M_PI_F * fract(float(i) * 0.7548776662 + pixel_rand_a);
        float rad = ghost_pupil * sqrt(fract(float(i) * 0.3247179572 + pixel_rand_r));
        float3 lens_point = float3(rad * cos(angle), rad * sin(angle), front_z);

        float3 ghost_ray_dir = normalize(lens_point - sensor_origin);

        for (int p = 0; p < scene.camera.num_ghost_pairs; p++) {
            int s1 = scene.camera.ghost_pairs[p * 2];
            int s2 = scene.camera.ghost_pairs[p * 2 + 1];

            float3 ghost_origin, ghost_dir;
            float gw = ray_ghost_through_lens(
                sensor_origin, ghost_ray_dir,
                scene.camera, wavelength, s1, s2,
                ghost_origin, ghost_dir);

            if (gw > 1e-7) {
                debug_exits += 1.0;
                debug_max_gw = max(debug_max_gw, gw);

                // Ghost source model: check if ghost ray direction points toward
                // bright light sources. The anamorphic cylinder creates a mapping
                // where different sensor pixels see the same light through the ghost
                // path — the asymmetric mapping creates the horizontal streak.
                float3 ghost_contrib = float3(0);

                for (int li = 0; li < scene.num_lights; li++) {
                    float3 lp = float3(scene.lights[li].px,
                                       scene.lights[li].py,
                                       scene.lights[li].pz);
                    float3 lc = float3(scene.lights[li].color_r,
                                       scene.lights[li].color_g,
                                       scene.lights[li].color_b);
                    float spec_mult = scene.lights[li].specular;

                    // Direction from ghost exit point to light
                    float3 to_light = normalize(lp - ghost_origin);
                    float cos_angle = dot(ghost_dir, to_light);

                    // Accept if ghost ray points closely toward the light
                    // Tight cone prevents blobs, preserves streak shape
                    if (cos_angle > 0.995) {
                        float light_dist = length(lp - ghost_origin);
                        float atten = 1.0 / (1.0 + light_dist * 0.01);
                        // Smooth falloff within the acceptance cone
                        float t = (cos_angle - 0.995) / 0.005;
                        float intensity = t * t * spec_mult * atten;
                        ghost_contrib += intensity * lc;
                        debug_glint_hits += 1.0;
                    }
                }

                // Also check scene hits for specular glints (point source reflections)
                float nearest_dist = 1e30;
                int hit_type = -1;
                int hit_idx = -1;
                float3 hit_point;

                for (int si = 0; si < scene.num_spheres; si++) {
                    if (scene.spheres[si].is_lens > 0.5) continue;
                    float3 sc = float3(scene.spheres[si].cx,
                                       scene.spheres[si].cy,
                                       scene.spheres[si].cz);
                    float sr = scene.spheres[si].radius;
                    float3 isect;
                    if (ray_sphere_intersection(sc, sr, ghost_origin, ghost_dir, isect)) {
                        float d = distance(isect, ghost_origin);
                        if (d > 0.001 && d < nearest_dist) {
                            nearest_dist = d;
                            hit_point = isect;
                            hit_type = 0;
                            hit_idx = si;
                        }
                    }
                }

                if (hit_type == 0) {
                    debug_hits += 1.0;
                    constant GPUSurface &surf = scene.spheres[hit_idx];
                    float3 center = float3(surf.cx, surf.cy, surf.cz);
                    float3 normal = normalize(hit_point - center);

                    // Emissive
                    if (surf.emission > 0) {
                        debug_emissive_hits += 1.0;
                        ghost_contrib += float3(surf.color_r, surf.color_g, surf.color_b) * surf.emission;
                    }

                    // Specular reflection of light sources at hit point
                    float3 refl = ghost_dir - 2.0 * dot(ghost_dir, normal) * normal;
                    for (int li = 0; li < scene.num_lights; li++) {
                        float3 lp = float3(scene.lights[li].px,
                                           scene.lights[li].py,
                                           scene.lights[li].pz);
                        float3 lc = float3(scene.lights[li].color_r,
                                           scene.lights[li].color_g,
                                           scene.lights[li].color_b);
                        float spec_mult = scene.lights[li].specular;
                        float3 to_light = normalize(lp - hit_point);
                        float refl_cos = dot(refl, to_light);
                        if (refl_cos > 0 && spec_mult > 0) {
                            float light_dist = length(lp - hit_point);
                            float atten = 1.0 / (1.0 + light_dist * 0.0001);
                            float ghost_phong = min(surf.phong, 500.0f);
                            ghost_contrib += pow(refl_cos, ghost_phong) * spec_mult * lc * atten;
                        }
                    }
                }

                ghost_contrib *= gw;
                ghost_contrib *= wavelength_to_rgb(wavelength, spec_norm);
                ghost_R += ghost_contrib.r;
                ghost_G += ghost_contrib.g;
                ghost_B += ghost_contrib.b;
            }
        }
    }

    // Normalize: average over GHOST_SAMPLES (Monte Carlo estimator)
    // Sum over ghost pairs is the physical total ghost contribution
    float ginv = 1.0 / float(GHOST_SAMPLES);
    float ghost_boost = 0.0;

    int gidx = pixel_idx * 3;
    ghost_buf[gidx + 0] = debug_emissive_hits + debug_glint_hits * 0.001;
    ghost_buf[gidx + 1] = debug_max_gw;
    ghost_buf[gidx + 2] = ghost_R + ghost_G + ghost_B;

    // Add ghost to main output
    output[gidx + 0] += ghost_R * ginv * ghost_boost;
    output[gidx + 1] += ghost_G * ginv * ghost_boost;
    output[gidx + 2] += ghost_B * ginv * ghost_boost;
}

// --- Source-driven streak kernel (cylinder-only transmission PSF) ---
// Traces rays from computed glint positions through ONLY the cylindrical lens
// element(s) via pure transmission. The cylinder refracts differently in X vs Y,
// creating a horizontally elongated PSF (streak) for each point source.
// Spherical elements are skipped to isolate the anamorphic line-spread effect.

kernel void flare_kernel(
    constant GPUScene &scene [[buffer(0)]],
    device atomic_float *flare_buf [[buffer(1)]],
    uint tid [[thread_position_in_grid]]
) {
    int width = scene.width;
    int height = scene.height;
    if (scene.camera.num_lenses <= 0) return;

    int total_lights = scene.num_lights;
    int samples_per_light = 67108864;
    int total_samples = total_lights * samples_per_light;
    if ((int)tid >= total_samples) return;

    int light_idx = tid / samples_per_light;
    int sample_idx = tid % samples_per_light;

    float3 light_pos = float3(scene.lights[light_idx].px,
                               scene.lights[light_idx].py,
                               scene.lights[light_idx].pz);
    float3 light_color = float3(scene.lights[light_idx].color_r,
                                 scene.lights[light_idx].color_g,
                                 scene.lights[light_idx].color_b);
    float spec_mult = scene.lights[light_idx].specular;
    if (spec_mult < 1.0) return;

    RNG rng(scene.frame_seed ^ tid * 2654435761u ^ uint(light_idx) * 7919u);
    float3 spec_norm = float3(scene.spec_norm_r, scene.spec_norm_g, scene.spec_norm_b);
    float wavelength = 380.0 + 400.0 * fract(float(sample_idx) * 0.6180339887 + rng.uniform());

    // Compute glint position on outermost glass sphere
    float3 best_center = float3(0, 0, -3.0);
    float best_radius = 1.5;
    for (int si = 0; si < scene.num_spheres; si++) {
        if (scene.spheres[si].phong > 100 && scene.spheres[si].transparency > 0.5) {
            if (scene.spheres[si].radius > best_radius) {
                best_center = float3(scene.spheres[si].cx, scene.spheres[si].cy, scene.spheres[si].cz);
                best_radius = scene.spheres[si].radius;
            }
        }
    }

    float3 cam_pos = float3(0, 0, scene.camera.cam_z);
    float3 glint_pos = best_center + best_radius * normalize(cam_pos - best_center);
    for (int iter = 0; iter < 5; iter++) {
        float3 v = normalize(cam_pos - glint_pos);
        float3 l = normalize(light_pos - glint_pos);
        float3 h = normalize(v + l);
        glint_pos = best_center + best_radius * h;
    }

    // Find the cylinder element
    int cyl_idx = -1;
    for (int e = 0; e < scene.camera.num_lenses; e++) {
        if (scene.camera.lenses[e].anamorphic > 0.5) { cyl_idx = e; break; }
    }
    if (cyl_idx < 0) return;
    constant GPULensElement &cyl = scene.camera.lenses[cyl_idx];

    // Sample entry on the cylinder's aperture
    float lens_radius = cyl.radius;
    float angle = 2.0 * M_PI_F * rng.uniform();
    float rad = lens_radius * sqrt(rng.uniform());
    float3 lens_point = float3(rad * cos(angle), rad * sin(angle), cyl.z);

    float3 dir = normalize(lens_point - glint_pos);
    float3 pos = lens_point;

    // Weight
    float glint_dist = length(lens_point - glint_pos);
    float cos_angle = max(abs(dir.z), 0.1f);
    float streak_boost = 5000.0;
    float weight = spec_mult * cos_angle * M_PI_F * lens_radius * lens_radius
                   * streak_boost / (glint_dist * glint_dist * float(samples_per_light));

    float cyl_ang = cyl.anamorphic;
    float ri = cauchy_ri(cyl.cauchy_a, cyl.cauchy_b, wavelength);
    float R_sq = cyl.radius * cyl.radius;

    // Trace through the cylinder's two surfaces only (cylinder-only transmission)
    // Back surface first (scene-facing, since the cylinder is the frontmost element
    // and the ray comes from the scene at negative z toward positive z)
    // Actually the cylinder is at z=0, the scene is at z=-3. The ray goes from
    // z=-1.5 toward z=0 (+z direction). It enters the cylinder back surface first.
    
    // The cylinder geometry: front_center_z and back_center_z are both at ~z=0.37 and z=0.43
    // The actual cylinder surfaces intersect the optical axis near z=0 (the lens z position)

    // Use the full lens traversal but ONLY for the cylinder element
    float z_margin = 1.0;
    float z_min = cyl.z - z_margin;
    float z_max = cyl.z + z_margin;

    for (int bounce = 0; bounce < 8; bounce++) {
        if (weight < 1e-10) break;

        float nearest_t = 1e30;
        float3 nearest_isect;
        float3 nearest_normal;
        float nearest_ri = ri;
        bool found = false;

        // Front surface
        float abs_r1 = abs(cyl.r1);
        float fc_z = cyl.front_center_z;
        float t0, t1;
        ray_cylinder_t(fc_z, abs_r1, cyl_ang, pos, dir, t0, t1);
        for (int hh = 0; hh < 2; hh++) {
            float t = (hh == 0) ? t0 : t1;
            if (t <= LENS_EPS || t >= nearest_t) continue;
            float3 isect = pos + t * dir;
            if (isect.z < z_min || isect.z > z_max) continue;
            if (isect.x * isect.x + isect.y * isect.y > R_sq) continue;
            float3 normal = cylinder_normal(isect, fc_z, cyl_ang);
            nearest_t = t; nearest_isect = isect; nearest_normal = normal;
            found = true;
        }

        // Back surface
        float abs_r2 = abs(cyl.r2);
        float bc_z = cyl.back_center_z;
        ray_cylinder_t(bc_z, abs_r2, cyl_ang, pos, dir, t0, t1);
        for (int hh = 0; hh < 2; hh++) {
            float t = (hh == 0) ? t0 : t1;
            if (t <= LENS_EPS || t >= nearest_t) continue;
            float3 isect = pos + t * dir;
            if (isect.z < z_min || isect.z > z_max) continue;
            if (isect.x * isect.x + isect.y * isect.y > R_sq) continue;
            float3 normal = cylinder_normal(isect, bc_z, cyl_ang);
            nearest_t = t; nearest_isect = isect; nearest_normal = normal;
            found = true;
        }

        if (!found) break;

        // Refract through surface
        bool entering = (bounce == 0 || bounce == 2);  // entering glass on first hit
        float n1 = entering ? 1.0 : ri;
        float n2 = entering ? ri : 1.0;
        float cos_i = abs(dot(dir, nearest_normal));
        float fresnel_r = fresnel_schlick(cos_i, n1, n2);
        weight *= (1.0 - fresnel_r);
        if (weight < 1e-10) break;

        float3 n = (dot(dir, nearest_normal) < 0) ? nearest_normal : -nearest_normal;
        float eta = n1 / n2;
        float3 refracted = refract(dir, n, eta);
        if (length(refracted) < 0.001) break;
        dir = refracted;
        pos = nearest_isect;
    }

    if (weight < 1e-10 || dir.z <= 0) return;

    float t_sensor = (scene.camera.cam_z - pos.z) / dir.z;
    if (t_sensor <= 0) return;

    float3 sensor_hit = pos + t_sensor * dir;
    float aspect = float(width) / float(height);
    float px = 0.5 - sensor_hit.x / (2.0 * scene.camera.cam_d * aspect);
    float py = 0.5 - sensor_hit.y / (2.0 * scene.camera.cam_d);

    // Bilinear splatting
    float fx = px * float(width) - 0.5;
    float fy = py * float(height) - 0.5;
    int ix0 = int(floor(fx));
    int iy0 = int(floor(fy));
    float sx = fx - float(ix0);
    float sy = fy - float(iy0);

    float3 spectral = wavelength_to_rgb(wavelength, spec_norm);
    float3 contribution = light_color * weight * spectral;

    for (int dy = 0; dy <= 1; dy++) {
        for (int dx = 0; dx <= 1; dx++) {
            int px_x = ix0 + dx;
            int px_y = iy0 + dy;
            if (px_x >= 0 && px_x < width && px_y >= 0 && px_y < height) {
                float wx = (dx == 0) ? (1.0 - sx) : sx;
                float wy = (dy == 0) ? (1.0 - sy) : sy;
                float w = wx * wy;
                int pidx = (px_y * width + px_x) * 3;
                atomic_fetch_add_explicit(&flare_buf[pidx + 0], contribution.r * w, memory_order_relaxed);
                atomic_fetch_add_explicit(&flare_buf[pidx + 1], contribution.g * w, memory_order_relaxed);
                atomic_fetch_add_explicit(&flare_buf[pidx + 2], contribution.b * w, memory_order_relaxed);
            }
        }
    }
}
