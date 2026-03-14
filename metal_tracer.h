#ifndef METAL_TRACER_H
#define METAL_TRACER_H

#include "tracer.h"

int  gpu_init(void);
void gpu_ray_trace_to_pixels(sceneT *scene, int width, int height,
                             int min_samples, int max_samples,
                             float qual_thresh, int trace_depth,
                             int shadow_rays,
                             char *pixels, float *pixels_f);

#endif
