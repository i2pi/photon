#import <Metal/Metal.h>
#import <Foundation/Foundation.h>
#include <math.h>
#include <string.h>
#include "metal_tracer.h"

// --- GPU struct definitions (must match tracer.metal exactly) ---

#define GPU_MAX_SPHERES  32
#define GPU_MAX_PLANES   8
#define GPU_MAX_LIGHTS   8
#define GPU_MAX_BVH_NODES 64

typedef struct {
    float cx, cy, cz, radius;
    float color_r, color_g, color_b, color_a;
    float reflectance, roughness, transparency, cauchy_a;
    float cauchy_b, pad_s1, pad_s2, pad_s3;
} GPUSurface;

typedef struct {
    float px, py, pz, pw;
    float color_r, color_g, color_b, color_a;
} GPULight;

typedef struct {
    float lens_z, lens_r1, lens_r2, lens_radius;
    float lens_cauchy_a, lens_cauchy_b, cam_d, cam_z;
    float has_lens, pad_c0, pad_c1, pad_c2;
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
    float pad0;
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

static float gpu_gamma(float x) {
    x = powf(x - 0.05f, 1.1f);
    if (x > 1.0f) x = 1.0f;
    if (x < 0.0f) x = 0.0f;
    return x;
}

void gpu_ray_trace_to_pixels(sceneT *scene, int width, int height,
                             int min_samples, int max_samples,
                             float qual_thresh, int trace_depth,
                             char *pixels, float *pixels_f) {
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
        gpu_scene.frame_seed = arc4random();

        // Camera
        gpu_scene.camera.cam_d = scene->camera.d;
        gpu_scene.camera.cam_z = scene->camera.z;

        if (scene->camera.lenses > 0) {
            lensT *lens = &scene->camera.lens[0];
            gpu_scene.camera.lens_z = lens->z;
            gpu_scene.camera.lens_r1 = lens->r1;
            gpu_scene.camera.lens_r2 = lens->r2;
            gpu_scene.camera.lens_radius = lens->radius;
            gpu_scene.camera.lens_cauchy_a = lens->cauchy_a;
            gpu_scene.camera.lens_cauchy_b = lens->cauchy_b;
            gpu_scene.camera.has_lens = 1.0f;
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
        }

        // Create buffers
        size_t output_size = sizeof(float) * width * height * 3;

        id<MTLBuffer> scene_buf = [mtl_device newBufferWithBytes:&gpu_scene
                                                          length:sizeof(GPUScene)
                                                         options:MTLResourceStorageModeShared];

        id<MTLBuffer> output_buf = [mtl_device newBufferWithLength:output_size
                                                           options:MTLResourceStorageModeShared];

        // Dispatch
        id<MTLCommandBuffer> cmd = [mtl_queue commandBuffer];
        id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];

        [enc setComputePipelineState:mtl_pipeline];
        [enc setBuffer:scene_buf offset:0 atIndex:0];
        [enc setBuffer:output_buf offset:0 atIndex:1];

        MTLSize grid = MTLSizeMake(width, height, 1);
        NSUInteger w = [mtl_pipeline threadExecutionWidth];
        NSUInteger h = [mtl_pipeline maxTotalThreadsPerThreadgroup] / w;
        MTLSize group = MTLSizeMake(w, h, 1);

        [enc dispatchThreads:grid threadsPerThreadgroup:group];
        [enc endEncoding];

        [cmd commit];
        [cmd waitUntilCompleted];

        if ([cmd error]) {
            fprintf(stderr, "Metal: execution error: %s\n",
                    [[[cmd error] localizedDescription] UTF8String]);
            return;
        }

        // Read back and convert
        float *gpu_output = (float *)[output_buf contents];

        for (int i = 0; i < width * height * 3; i++) {
            pixels_f[i] = gpu_output[i];
        }

        for (int i = 0; i < width * height; i++) {
            int idx = i * 3;
            pixels[idx + 0] = (char)(gpu_gamma(gpu_output[idx + 0]) * 255);
            pixels[idx + 1] = (char)(gpu_gamma(gpu_output[idx + 1]) * 255);
            pixels[idx + 2] = (char)(gpu_gamma(gpu_output[idx + 2]) * 255);
        }
    }
}
