#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "tracer.h"
#include "scene_parser.h"

#define MAX_LINE 1024
#define MAX_TOKENS 32

static int tokenize(char *line, char *tokens[], int max) {
	int n = 0;
	char *p = line;

	while (*p && n < max) {
		while (*p && isspace(*p)) p++;
		if (!*p || *p == '#') break;

		char *start = p;
		while (*p && !isspace(*p) && *p != '#') p++;
		if (*p) *p++ = '\0';
		tokens[n++] = start;
	}
	return n;
}

static float parse_float(const char *s) {
	return (float)atof(s);
}

static int parse_int(const char *s) {
	return atoi(s);
}

/*
 * Find "key=value" in tokens. Returns value string or NULL.
 */
static const char *find_kv(char *tokens[], int n, const char *key) {
	int klen = strlen(key);
	for (int i = 0; i < n; i++) {
		if (strncmp(tokens[i], key, klen) == 0 && tokens[i][klen] == '=') {
			return &tokens[i][klen + 1];
		}
	}
	return NULL;
}

static float get_kv_float(char *tokens[], int n, const char *key, float def) {
	const char *v = find_kv(tokens, n, key);
	return v ? parse_float(v) : def;
}

static int get_kv_int(char *tokens[], int n, const char *key, int def) {
	const char *v = find_kv(tokens, n, key);
	return v ? parse_int(v) : def;
}

/*
 * Parse "r,g,b" or "r,g,b,a" into a float[4]. Alpha defaults to 1.
 */
static void parse_color(const char *s, float color[4]) {
	color[0] = color[1] = color[2] = 0;
	color[3] = 1;
	sscanf(s, "%f,%f,%f,%f", &color[0], &color[1], &color[2], &color[3]);
}

/*
 * Parse "x,y,z" into three floats.
 */
static void parse_vec3(const char *s, float *x, float *y, float *z) {
	*x = *y = *z = 0;
	sscanf(s, "%f,%f,%f", x, y, z);
}

sceneT *load_scene(const char *filename, render_settingsT *settings) {
	FILE *fp = fopen(filename, "r");
	if (!fp) {
		fprintf(stderr, "scene: can't open %s\n", filename);
		return NULL;
	}

	/* Defaults */
	settings->width = 1920;
	settings->height = 1080;
	settings->min_samples = 128;
	settings->max_samples = 1000;
	settings->qual_thresh = 0.1f;
	settings->trace_depth = 8;

	sceneT *s = create_scene();
	char line[MAX_LINE];
	int lineno = 0;

	while (fgets(line, MAX_LINE, fp)) {
		lineno++;
		char *tokens[MAX_TOKENS];
		int n = tokenize(line, tokens, MAX_TOKENS);
		if (n == 0) continue;

		const char *cmd = tokens[0];

		if (strcmp(cmd, "render") == 0) {
			settings->width = get_kv_int(tokens, n, "width", settings->width);
			settings->height = get_kv_int(tokens, n, "height", settings->height);
			settings->min_samples = get_kv_int(tokens, n, "min_samples", settings->min_samples);
			settings->max_samples = get_kv_int(tokens, n, "max_samples", settings->max_samples);
			settings->qual_thresh = get_kv_float(tokens, n, "qual_thresh", settings->qual_thresh);
			settings->trace_depth = get_kv_int(tokens, n, "trace_depth", settings->trace_depth);

		} else if (strcmp(cmd, "camera") == 0) {
			s->camera.z = get_kv_float(tokens, n, "z", s->camera.z);
			s->camera.d = get_kv_float(tokens, n, "d", s->camera.d);

		} else if (strcmp(cmd, "lens") == 0) {
			float z  = get_kv_float(tokens, n, "z", 0);
			float r1 = get_kv_float(tokens, n, "r1", 1);
			float r2 = get_kv_float(tokens, n, "r2", 1);
			float r  = get_kv_float(tokens, n, "radius", 0.1);
			float ca = get_kv_float(tokens, n, "cauchy_a", 1.5);
			float cb = get_kv_float(tokens, n, "cauchy_b", 0.0);
			add_lens_to_camera(&s->camera, z, r1, r2, r, ca, cb);
			add_object_to_scene(s, s->camera.lens[s->camera.lenses - 1].object);

		} else if (strcmp(cmd, "sphere") == 0) {
			float x = 0, y = 0, z = 0;
			const char *pos = find_kv(tokens, n, "pos");
			if (pos) parse_vec3(pos, &x, &y, &z);
			float radius = get_kv_float(tokens, n, "radius", 1.0);

			float color[4] = {0.8, 0.8, 0.8, 1.0};
			const char *cv = find_kv(tokens, n, "color");
			if (cv) parse_color(cv, color);

			float refl  = get_kv_float(tokens, n, "reflectance", 0.0);
			float rough = get_kv_float(tokens, n, "roughness", 0.0);
			float trans = get_kv_float(tokens, n, "transparency", 0.0);
			float ca    = get_kv_float(tokens, n, "cauchy_a", 1.0);
			float cb    = get_kv_float(tokens, n, "cauchy_b", 0.0);

			objectT *obj = create_sphere_object(x, y, z, radius);
			color_object(obj, color, refl, rough, trans, ca, cb);
			add_object_to_scene(s, obj);

		} else if (strcmp(cmd, "plane") == 0) {
			float nx = 0, ny = 1, nz = 0;
			const char *nv = find_kv(tokens, n, "normal");
			if (nv) parse_vec3(nv, &nx, &ny, &nz);
			float d = get_kv_float(tokens, n, "d", 0.0);

			float color[4] = {0.8, 0.8, 0.8, 1.0};
			const char *cv = find_kv(tokens, n, "color");
			if (cv) parse_color(cv, color);

			float refl  = get_kv_float(tokens, n, "reflectance", 0.0);
			float rough = get_kv_float(tokens, n, "roughness", 0.0);
			float trans = get_kv_float(tokens, n, "transparency", 0.0);
			float ca    = get_kv_float(tokens, n, "cauchy_a", 1.0);
			float cb    = get_kv_float(tokens, n, "cauchy_b", 0.0);

			objectT *obj = create_ortho_plane_object(nx, ny, nz, d);
			color_object(obj, color, refl, rough, trans, ca, cb);
			add_object_to_scene(s, obj);

		} else if (strcmp(cmd, "light") == 0) {
			float x = 0, y = 0, z = 0;
			const char *pos = find_kv(tokens, n, "pos");
			if (pos) parse_vec3(pos, &x, &y, &z);

			float color[4] = {1.0, 1.0, 1.0, 1.0};
			const char *cv = find_kv(tokens, n, "color");
			if (cv) {
				parse_color(cv, color);
			} else {
				/* Shorthand: intensity=0.8 sets uniform grey */
				float I = get_kv_float(tokens, n, "intensity", 1.0);
				color[0] = color[1] = color[2] = color[3] = I;
			}

			lightT *l = create_positional_light(x, y, z, color);
			add_light_to_scene(s, l);

		} else {
			fprintf(stderr, "scene:%d: unknown command '%s'\n", lineno, cmd);
		}
	}

	fclose(fp);
	return s;
}
