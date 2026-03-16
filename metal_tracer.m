#import <Metal/Metal.h>
#import <Foundation/Foundation.h>
#include <math.h>
#include <string.h>
#include <sys/time.h>
#include "metal_tracer.h"
#include "scene_parser.h"

// --- GPU struct definitions (must match tracer.metal exactly) ---

#define GPU_MAX_SPHERES  32
#define GPU_MAX_PLANES   8
#define GPU_MAX_LIGHTS   8
#define GPU_MAX_BVH_NODES 64
#define GPU_MAX_LENSES   8

typedef struct {
    float cx, cy, cz, radius;
    float color_r, color_g, color_b, color_a;
    float reflectance, roughness, transparency, cauchy_a;
    float cauchy_b, emission, phong, is_lens;
} GPUSurface;

typedef struct {
    float px, py, pz, pw;
    float color_r, color_g, color_b, color_a;
    float specular, diffuse_mult, pad_l2, pad_l3;
} GPULight;

typedef struct {
    float z, r1, r2, radius;
    float cauchy_a, cauchy_b, reflectance, anamorphic;
    float front_center_z, back_center_z;
    float pad_le0, pad_le1;
} GPULensElement;

typedef struct {
    float cam_d, cam_z;
    int num_lenses, pad_cam;
    float aperture_z, aperture_radius, pad_a0, pad_a1;
    GPULensElement lenses[GPU_MAX_LENSES];
    int ghost_pairs[90 * 2];
    int num_ghost_pairs;
    int pad_gp;
} GPUCamera;

typedef struct {
    float min_x, min_y, min_z, pad0;
    float max_x, max_y, max_z, pad1;
    int left, right;
    int prim_start, prim_count;
} BVHNode;

typedef struct {
    int width, height;
    int min_samples, max_samples;
    float qual_thresh;
    int trace_depth;
    unsigned int frame_seed;
    int num_spheres;
    int num_planes;
    int num_lights;
    int num_bvh_nodes;
    int shadow_rays;
    int ghost_rays;
    int sample_offset;
    int batch_size;
    float pad0;
    float spec_norm_r, spec_norm_g, spec_norm_b, pad_sn;
    GPUCamera camera;
    GPUSurface spheres[GPU_MAX_SPHERES];
    GPUSurface planes[GPU_MAX_PLANES];
    GPULight lights[GPU_MAX_LIGHTS];
    BVHNode bvh[GPU_MAX_BVH_NODES];
    int bvh_prim_indices[GPU_MAX_SPHERES];
} GPUScene;

// --- Metal state ---

static id<MTLDevice>              mtl_device;
static id<MTLCommandQueue>        mtl_queue;
static id<MTLComputePipelineState> mtl_pipeline;
static id<MTLComputePipelineState> mtl_ghost_pipeline;
static int gpu_ready = 0;

int gpu_init(void) {
    @autoreleasepool {
        mtl_device = MTLCreateSystemDefaultDevice();
        if (!mtl_device) {
            fprintf(stderr, "Metal: no GPU device found\n");
            return -1;
        }

        printf("Metal: using %s\n", [[mtl_device name] UTF8String]);

        mtl_queue = [mtl_device newCommandQueue];

        // Load shader source from file
        NSString *path = [[NSBundle mainBundle] pathForResource:@"tracer" ofType:@"metal"];
        if (!path) {
            // Try current directory
            path = @"tracer.metal";
        }

        NSError *error = nil;
        NSString *source = [NSString stringWithContentsOfFile:path
                                                     encoding:NSUTF8StringEncoding
                                                        error:&error];
        if (!source) {
            fprintf(stderr, "Metal: failed to load tracer.metal: %s\n",
                    [[error localizedDescription] UTF8String]);
            return -1;
        }

        MTLCompileOptions *opts = [[MTLCompileOptions alloc] init];
        opts.mathMode = MTLMathModeFast;

        id<MTLLibrary> library = [mtl_device newLibraryWithSource:source
                                                          options:opts
                                                            error:&error];
        if (!library) {
            fprintf(stderr, "Metal: shader compile error: %s\n",
                    [[error localizedDescription] UTF8String]);
            return -1;
        }

        id<MTLFunction> kernel = [library newFunctionWithName:@"trace_kernel"];
        if (!kernel) {
            fprintf(stderr, "Metal: trace_kernel not found\n");
            return -1;
        }

        mtl_pipeline = [mtl_device newComputePipelineStateWithFunction:kernel error:&error];
        if (!mtl_pipeline) {
            fprintf(stderr, "Metal: pipeline error: %s\n",
                    [[error localizedDescription] UTF8String]);
            return -1;
        }

        id<MTLFunction> ghost_fn = [library newFunctionWithName:@"ghost_kernel"];
        if (ghost_fn) {
            mtl_ghost_pipeline = [mtl_device newComputePipelineStateWithFunction:ghost_fn error:&error];
            if (!mtl_ghost_pipeline) {
                fprintf(stderr, "Metal: ghost pipeline error: %s\n",
                        [[error localizedDescription] UTF8String]);
            }
        }

        printf("Metal: pipeline ready (max threads/group: %lu)\n",
               (unsigned long)[mtl_pipeline maxTotalThreadsPerThreadgroup]);

        gpu_ready = 1;
        return 0;
    }
}

static void pack_surface(objectT *obj, int surf_idx, GPUSurface *out) {
    surfaceT *s = &obj->surface[surf_idx];
    float *p = s->parameter;

    out->cx = p[0];
    out->cy = p[1];
    out->cz = p[2];
    out->radius = p[3];

    out->color_r = s->properties.color[0];
    out->color_g = s->properties.color[1];
    out->color_b = s->properties.color[2];
    out->color_a = s->properties.color[3];

    out->reflectance = s->properties.reflectance;
    out->roughness = s->properties.roughness;
    out->transparency = s->properties.transparency;
    out->cauchy_a = s->properties.cauchy_a;
    out->cauchy_b = s->properties.cauchy_b;
    out->emission = s->properties.emission;
    out->phong = s->properties.phong;
    out->is_lens = s->properties.is_lens;
}

// --- BVH build (top-down median split) ---

static BVHNode bvh_nodes_buf[GPU_MAX_BVH_NODES];
static int bvh_node_count;

static float sphere_centroid(GPUSurface *s, int axis) {
    if (axis == 0) return s->cx;
    if (axis == 1) return s->cy;
    return s->cz;
}

static int build_bvh(GPUSurface *spheres, int *indices, int start, int count) {
    if (bvh_node_count >= GPU_MAX_BVH_NODES) return -1;

    int ni = bvh_node_count++;
    BVHNode *node = &bvh_nodes_buf[ni];

    // Compute AABB over primitives in [start, start+count)
    float bmin[3] = {1e30f, 1e30f, 1e30f};
    float bmax[3] = {-1e30f, -1e30f, -1e30f};
    for (int i = start; i < start + count; i++) {
        GPUSurface *s = &spheres[indices[i]];
        float r = s->radius;
        float c[3] = {s->cx, s->cy, s->cz};
        for (int a = 0; a < 3; a++) {
            if (c[a] - r < bmin[a]) bmin[a] = c[a] - r;
            if (c[a] + r > bmax[a]) bmax[a] = c[a] + r;
        }
    }
    node->min_x = bmin[0]; node->min_y = bmin[1]; node->min_z = bmin[2]; node->pad0 = 0;
    node->max_x = bmax[0]; node->max_y = bmax[1]; node->max_z = bmax[2]; node->pad1 = 0;

    if (count <= 2) {
        node->left = -1;
        node->right = -1;
        node->prim_start = start;
        node->prim_count = count;
        return ni;
    }

    // Find longest axis
    int axis = 0;
    float extent = bmax[0] - bmin[0];
    for (int a = 1; a < 3; a++) {
        float e = bmax[a] - bmin[a];
        if (e > extent) { extent = e; axis = a; }
    }

    // Insertion sort by centroid on chosen axis
    for (int i = start + 1; i < start + count; i++) {
        int key = indices[i];
        float kv = sphere_centroid(&spheres[key], axis);
        int j = i - 1;
        while (j >= start && sphere_centroid(&spheres[indices[j]], axis) > kv) {
            indices[j + 1] = indices[j];
            j--;
        }
        indices[j + 1] = key;
    }

    int mid = count / 2;
    node->prim_start = 0;
    node->prim_count = 0;
    node->left = build_bvh(spheres, indices, start, mid);
    node->right = build_bvh(spheres, indices, start + mid, count - mid);
    return ni;
}

void gpu_ray_trace_to_pixels(sceneT *scene, int width, int height,
                             int min_samples, int max_samples,
                             float qual_thresh, int trace_depth,
                             int shadow_rays, int ghost_rays,
                             char *pixels, float *pixels_f,
                             render_settingsT *settings) {
    if (!gpu_ready) {
        fprintf(stderr, "Metal: not initialized\n");
        return;
    }

    @autoreleasepool {
        // Pack scene
        GPUScene gpu_scene;
        memset(&gpu_scene, 0, sizeof(GPUScene));

        gpu_scene.width = width;
        gpu_scene.height = height;
        gpu_scene.min_samples = min_samples;
        gpu_scene.max_samples = max_samples;
        gpu_scene.qual_thresh = qual_thresh;
        gpu_scene.trace_depth = trace_depth;
        gpu_scene.shadow_rays = shadow_rays;
        gpu_scene.ghost_rays = ghost_rays;
        gpu_scene.frame_seed = arc4random();

        // Precompute spectral normalization (same as GPU compute_spectral_norm)
        {
            float rs = 0, gs = 0, bs = 0;
            int N = 1000;
            for (int i = 0; i < N; i++) {
                float wl = 380.0f + 400.0f * i / (float)N;
                float r = 0, g = 0, b = 0;
                if (wl >= 380 && wl < 440) { r = -(wl-440)/(440-380); b = 1; }
                else if (wl < 490) { g = (wl-440)/(490-440); b = 1; }
                else if (wl < 510) { g = 1; b = -(wl-510)/(510-490); }
                else if (wl < 580) { r = (wl-510)/(580-510); g = 1; }
                else if (wl < 645) { r = 1; g = -(wl-645)/(645-580); }
                else if (wl <= 780) { r = 1; }
                float t;
                if (wl >= 380 && wl < 420) t = 0.3f + 0.7f*(wl-380)/(420-380);
                else if (wl >= 700) t = 0.3f + 0.7f*(780-wl)/(780-700);
                else t = 1.0f;
                rs += r*t; gs += g*t; bs += b*t;
            }
            gpu_scene.spec_norm_r = (float)N / rs;
            gpu_scene.spec_norm_g = (float)N / gs;
            gpu_scene.spec_norm_b = (float)N / bs;
        }

        // Camera
        gpu_scene.camera.cam_d = scene->camera.d;
        gpu_scene.camera.cam_z = scene->camera.z;
        gpu_scene.camera.aperture_z = scene->camera.aperture_z;
        gpu_scene.camera.aperture_radius = scene->camera.aperture_radius;
        gpu_scene.camera.num_lenses = scene->camera.lenses;
        if (gpu_scene.camera.num_lenses > GPU_MAX_LENSES)
            gpu_scene.camera.num_lenses = GPU_MAX_LENSES;

        for (int i = 0; i < gpu_scene.camera.num_lenses; i++) {
            lensT *lens = &scene->camera.lens[i];
            gpu_scene.camera.lenses[i].z = lens->z;
            gpu_scene.camera.lenses[i].r1 = lens->r1;
            gpu_scene.camera.lenses[i].r2 = lens->r2;
            gpu_scene.camera.lenses[i].radius = lens->radius;
            gpu_scene.camera.lenses[i].cauchy_a = lens->cauchy_a;
            gpu_scene.camera.lenses[i].cauchy_b = lens->cauchy_b;
            gpu_scene.camera.lenses[i].reflectance = lens->reflectance;
            gpu_scene.camera.lenses[i].anamorphic = lens->anamorphic;

            float abs_r1 = fabsf(lens->r1);
            float sign1 = (lens->r1 > 0) ? 1.0f : -1.0f;
            gpu_scene.camera.lenses[i].front_center_z = lens->z - sign1 * sqrtf(abs_r1 * abs_r1 - lens->radius * lens->radius);

            float abs_r2 = fabsf(lens->r2);
            float sign2 = (lens->r2 > 0) ? 1.0f : -1.0f;
            gpu_scene.camera.lenses[i].back_center_z = lens->z + sign2 * sqrtf(abs_r2 * abs_r2 - lens->radius * lens->radius);
        }

        // Precompute valid ghost pairs (only those involving anamorphic elements)
        gpu_scene.camera.num_ghost_pairs = 0;
        int num_surfaces = gpu_scene.camera.num_lenses * 2;
        for (int s1 = 1; s1 < num_surfaces; s1++) {
            for (int s2 = 0; s2 < s1; s2++) {
                int e1 = s1 / 2, e2 = s2 / 2;
                if (scene->camera.lens[e1].anamorphic > 0.5f ||
                    scene->camera.lens[e2].anamorphic > 0.5f) {
                    int idx = gpu_scene.camera.num_ghost_pairs * 2;
                    gpu_scene.camera.ghost_pairs[idx] = s1;
                    gpu_scene.camera.ghost_pairs[idx + 1] = s2;
                    gpu_scene.camera.num_ghost_pairs++;
                }
            }
        }

        // Pack objects: separate spheres and planes
        // (skip lens objects)
        int n_spheres = 0, n_planes = 0;

        for (int i = 0; i < scene->objects; i++) {
            objectT *obj = scene->object[i];
            for (int j = 0; j < obj->surfaces; j++) {
                surfaceT *s = &obj->surface[j];
                if (s->primitive->ray_intersects == NULL) continue;

                // Identify by primitive name
                if (strcmp(s->primitive->name, "sphere") == 0 && n_spheres < GPU_MAX_SPHERES) {
                    pack_surface(obj, j, &gpu_scene.spheres[n_spheres++]);
                } else if (strcmp(s->primitive->name, "ortho_plane") == 0 && n_planes < GPU_MAX_PLANES) {
                    pack_surface(obj, j, &gpu_scene.planes[n_planes++]);
                }
                // skip lens, triangle for now
            }
        }

        gpu_scene.num_spheres = n_spheres;
        gpu_scene.num_planes = n_planes;

        // Build BVH over spheres
        if (n_spheres > 0) {
            int indices[GPU_MAX_SPHERES];
            for (int i = 0; i < n_spheres; i++) indices[i] = i;
            bvh_node_count = 0;
            build_bvh(gpu_scene.spheres, indices, 0, n_spheres);
            gpu_scene.num_bvh_nodes = bvh_node_count;
            memcpy(gpu_scene.bvh, bvh_nodes_buf, sizeof(BVHNode) * bvh_node_count);
            memcpy(gpu_scene.bvh_prim_indices, indices, sizeof(int) * n_spheres);
        } else {
            gpu_scene.num_bvh_nodes = 0;
        }

        // Lights
        gpu_scene.num_lights = scene->lights < GPU_MAX_LIGHTS ? scene->lights : GPU_MAX_LIGHTS;
        for (int i = 0; i < gpu_scene.num_lights; i++) {
            lightT *l = scene->light[i];
            gpu_scene.lights[i].px = l->position.x;
            gpu_scene.lights[i].py = l->position.y;
            gpu_scene.lights[i].pz = l->position.z;
            gpu_scene.lights[i].pw = 1.0f;
            gpu_scene.lights[i].color_r = l->color[0];
            gpu_scene.lights[i].color_g = l->color[1];
            gpu_scene.lights[i].color_b = l->color[2];
            gpu_scene.lights[i].color_a = l->color[3];
            gpu_scene.lights[i].specular = l->specular;
            gpu_scene.lights[i].diffuse_mult = l->diffuse_mult;
        }

        // Create buffers
        size_t output_size = sizeof(float) * width * height * 3;
        size_t accum_size = sizeof(float) * width * height * 10; // R,G,B,count,running_mean,M2,ghostR,ghostG,ghostB,ghost_n

        id<MTLBuffer> output_buf = [mtl_device newBufferWithLength:output_size
                                                           options:MTLResourceStorageModeShared];

        id<MTLBuffer> accum_buf = [mtl_device newBufferWithLength:accum_size
                                                          options:MTLResourceStorageModeShared];
        // Zero accumulation buffer
        memset([accum_buf contents], 0, accum_size);

        MTLSize grid = MTLSizeMake(width, height, 1);
        NSUInteger w = [mtl_pipeline threadExecutionWidth];
        NSUInteger h = [mtl_pipeline maxTotalThreadsPerThreadgroup] / w;
        MTLSize group = MTLSizeMake(w, h, 1);

        // Dispatch in batches to avoid GPU watchdog timeout
        int batch = 64;
        struct timeval batch_start, batch_end;

        // Create scene buffer once, update per-batch fields in place
        id<MTLBuffer> scene_buf = [mtl_device newBufferWithBytes:&gpu_scene
                                                          length:sizeof(GPUScene)
                                                         options:MTLResourceStorageModeShared];

        for (int offset = 0; offset < max_samples; offset += batch) {
            GPUScene *buf_ptr = (GPUScene *)[scene_buf contents];
            buf_ptr->sample_offset = offset;
            buf_ptr->batch_size = batch;

            id<MTLCommandBuffer> cmd = [mtl_queue commandBuffer];
            id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];

            [enc setComputePipelineState:mtl_pipeline];
            [enc setBuffer:scene_buf offset:0 atIndex:0];
            [enc setBuffer:output_buf offset:0 atIndex:1];
            [enc setBuffer:accum_buf offset:0 atIndex:2];

            [enc dispatchThreads:grid threadsPerThreadgroup:group];
            [enc endEncoding];

            gettimeofday(&batch_start, NULL);
            [cmd commit];
            [cmd waitUntilCompleted];
            gettimeofday(&batch_end, NULL);

            float batch_ms = ((batch_end.tv_sec - batch_start.tv_sec) * 1000.0f +
                             (batch_end.tv_usec - batch_start.tv_usec) / 1000.0f);
            fprintf(stderr, "  batch %2d (samples %4d-%4d): %.1fms\n",
                    offset / batch, offset, offset + batch - 1, batch_ms);

            if ([cmd error]) {
                fprintf(stderr, "  Metal: GPU warning batch %d (code %ld): %s\n",
                        offset / batch,
                        (long)[[cmd error] code],
                        [[[cmd error] localizedDescription] UTF8String]);
            }
        }

        // Dispatch ghost kernel (single pass, fixed sample count)
        if (ghost_rays && mtl_ghost_pipeline) {
            size_t ghost_size = sizeof(float) * width * height * 3;
            id<MTLBuffer> ghost_buf = [mtl_device newBufferWithLength:ghost_size
                                                              options:MTLResourceStorageModeShared];
            memset([ghost_buf contents], 0, ghost_size);

            NSUInteger gw = [mtl_ghost_pipeline threadExecutionWidth];
            NSUInteger gh = [mtl_ghost_pipeline maxTotalThreadsPerThreadgroup] / gw;
            MTLSize ghost_group = MTLSizeMake(gw, gh, 1);

            id<MTLCommandBuffer> ghost_cmd = [mtl_queue commandBuffer];
            id<MTLComputeCommandEncoder> ghost_enc = [ghost_cmd computeCommandEncoder];
            [ghost_enc setComputePipelineState:mtl_ghost_pipeline];
            [ghost_enc setBuffer:scene_buf offset:0 atIndex:0];
            [ghost_enc setBuffer:output_buf offset:0 atIndex:1];
            [ghost_enc setBuffer:ghost_buf offset:0 atIndex:2];
            [ghost_enc dispatchThreads:grid threadsPerThreadgroup:ghost_group];
            [ghost_enc endEncoding];

            struct timeval gs, ge;
            gettimeofday(&gs, NULL);
            [ghost_cmd commit];
            [ghost_cmd waitUntilCompleted];
            gettimeofday(&ge, NULL);
            float ghost_ms = ((ge.tv_sec - gs.tv_sec) * 1000.0f +
                             (ge.tv_usec - gs.tv_usec) / 1000.0f);
            fprintf(stderr, "  ghost pass: %.1fms\n", ghost_ms);
        }

        // Read back and convert
        float *gpu_output = (float *)[output_buf contents];

        for (int i = 0; i < width * height * 3; i++) {
            pixels_f[i] = gpu_output[i];
        }

        for (int i = 0; i < width * height; i++) {
            int idx = i * 3;
            float r = gpu_output[idx + 0];
            float g = gpu_output[idx + 1];
            float b = gpu_output[idx + 2];
            if (r < 0) r = 0; if (g < 0) g = 0; if (b < 0) b = 0;
            r *= settings->exposure; g *= settings->exposure; b *= settings->exposure;
            // Luminance-based ACES filmic tonemapping
            float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
            if (lum > 0.0001f) {
                float L = lum;
                float mapped = (L * (2.51f * L + 0.03f)) / (L * (2.43f * L + 0.59f) + 0.14f);
                if (mapped > 1.0f) mapped = 1.0f;
                float scale = mapped / lum;
                r *= scale; g *= scale; b *= scale;
            }
            // Contrast around midpoint
            if (settings->contrast != 1.0f) {
                r = 0.5f + (r - 0.5f) * settings->contrast;
                g = 0.5f + (g - 0.5f) * settings->contrast;
                b = 0.5f + (b - 0.5f) * settings->contrast;
            }
            // Saturation
            if (settings->saturation != 1.0f) {
                float grey = 0.2126f * r + 0.7152f * g + 0.0722f * b;
                r = grey + (r - grey) * settings->saturation;
                g = grey + (g - grey) * settings->saturation;
                b = grey + (b - grey) * settings->saturation;
            }
            if (r < 0.0f) r = 0.0f; if (g < 0.0f) g = 0.0f; if (b < 0.0f) b = 0.0f;
            if (r > 1.0f) r = 1.0f;
            if (g > 1.0f) g = 1.0f;
            if (b > 1.0f) b = 1.0f;
            pixels[idx + 0] = (char)(r * 255);
            pixels[idx + 1] = (char)(g * 255);
            pixels[idx + 2] = (char)(b * 255);
        }

        // Diagnostic analysis from accumulation buffer
        float *ac = (float *)[accum_buf contents];
        float total_samples = 0;
        float min_samples_px = 1e30, max_samples_px = 0;
        int num_pixels = width * height;
        int hist[5] = {0, 0, 0, 0, 0};

        for (int i = 0; i < num_pixels; i++) {
            float s = ac[i * 10 + 3];
            total_samples += s;
            if (s < min_samples_px) min_samples_px = s;
            if (s > max_samples_px) max_samples_px = s;

            if (s <= 128) hist[0]++;
            else if (s <= 256) hist[1]++;
            else if (s <= 512) hist[2]++;
            else if (s <= 768) hist[3]++;
            else hist[4]++;
        }

        fprintf(stderr, "\n--- GPU Diagnostics ---\n");
        fprintf(stderr, "Samples/pixel:  avg=%.0f  min=%.0f  max=%.0f\n",
                total_samples / num_pixels, min_samples_px, max_samples_px);
        fprintf(stderr, "Sample histogram: [0-128]=%d  [129-256]=%d  [257-512]=%d  [513-768]=%d  [769+]=%d\n",
                hist[0], hist[1], hist[2], hist[3], hist[4]);
        fprintf(stderr, "Total rays: %.0fM  Batches: %d\n",
                total_samples / 1e6, (max_samples + batch - 1) / batch);

        // Check variance values at a few pixels
        for (int py = height/2; py <= height/2; py++) {
            for (int px = width/4; px <= 3*width/4; px += width/4) {
                int pi = py * width + px;
                int ai = pi * 10;
                float n = ac[ai + 3];
                float mean = ac[ai + 4];
                float m2 = ac[ai + 5];
                float var = (n > 0) ? m2 / n : 0;
                float se = (n > 1) ? sqrtf(var / n) : 0;
                float rel = se / fmaxf(mean, 0.0001f);
                fprintf(stderr, "  pixel(%d,%d): n=%.0f mean=%.4f var=%.4f se=%.6f rel_err=%.4f\n",
                        px, py, n, mean, var, se, rel);
            }
        }
    }
}
