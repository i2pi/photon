#ifndef __SCENE_PARSER_H__
#define __SCENE_PARSER_H__

#include "tracer.h"

typedef struct {
	int		width, height;
	int		min_samples, max_samples;
	float	qual_thresh;
	int		trace_depth;
} render_settingsT;

sceneT *load_scene(const char *filename, render_settingsT *settings);

#endif
