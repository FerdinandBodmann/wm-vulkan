#ifndef PHONE_ANIM_H
#define PHONE_ANIM_H
#include <wlr/types/wlr_scene.h>
#include <wayland-server-core.h>

struct phone_server;

#define ANIM_DURATION_MS 220
#define ANIM_DURATION_SHORT 160
#define ANIM_DURATION_LONG 320

// Easing: easeOutCubic
double anim_ease_out_cubic(double t);

// Slide a tree vertically/horizontally from from to to over duration, then call done
void anim_slide_y(struct phone_server *server, struct wlr_scene_tree *tree,
                  int from_y, int to_y, int duration_ms,
                  void (*on_done)(void *data), void *data);
void anim_slide_x(struct phone_server *server, struct wlr_scene_tree *tree,
                  int from_x, int to_x, int duration_ms,
                  void (*on_done)(void *data), void *data);

// Cancel any running anim for tree
void anim_cancel(struct wlr_scene_tree *tree);

// Scale helper: logical output size (accounts for output->scale)
void anim_get_logical_size(struct phone_server *server, int *w, int *h, float *scale);

// Responsive sizing: compute icon size based on width
int anim_scaled_icon(int base, float scale_factor);
int anim_scaled(int base, float scale_factor);

#endif
