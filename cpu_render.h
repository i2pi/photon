#ifndef CPU_RENDER_H
#define CPU_RENDER_H

#include "tracer.h"

void ray_trace_to_pixels(sceneT *scene, int width, int height,
                         int min_samples, int max_samples,
                         float qual_thresh, int trace_depth,
                         char *pixels, float *pixels_f);

#endif
