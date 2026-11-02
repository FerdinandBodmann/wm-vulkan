#define _POSIX_C_SOURCE 200809L
#include "anim.h"
#include "server.h"
#include <math.h>
#include <stdlib.h>
#include <wlr/util/log.h>

struct anim_entry {
  struct wl_list link;
  struct wlr_scene_tree *tree;
  int start_y, end_y;
  int start_x, end_x;
  bool is_x;
  int duration;
  int elapsed;
  struct wl_event_source *timer;
  struct phone_server *server;
  void (*on_done)(void *data);
  void *data;
};

static struct wl_list active_anims;
static bool anims_inited = false;

static void anims_init(void) {
  if (!anims_inited) {
    wl_list_init(&active_anims);
    anims_inited = true;
  }
}

double anim_ease_out_cubic(double t) {
  double u = 1.0 - t;
  return 1.0 - u*u*u;
}

void anim_get_logical_size(struct phone_server *server, int *w, int *h, float *scale) {
  if (w) *w = 800;
  if (h) *h = 480;
  if (scale) *scale = 1.0f;
  if (!server || wl_list_empty(&server->outputs)) return;
  struct phone_output *out = wl_container_of(server->outputs.next, out, link);
  if (!out || !out->wlr_output) return;
  float sc = out->wlr_output->scale;
  if (sc < 1.0f) sc = 1.0f;
  if (scale) *scale = sc;
  // Use output_layout box for logical size (if available)
  struct wlr_box box;
  wlr_output_layout_get_box(server->output_layout, out->wlr_output, &box);
  if (box.width > 0 && box.height > 0) {
    if (w) *w = box.width;
    if (h) *h = box.height;
    return;
  }
  // fallback to physical / scale
  if (w) *w = (int)(out->wlr_output->width / sc);
  if (h) *h = (int)(out->wlr_output->height / sc);
  if (w && *w < 100) *w = out->wlr_output->width;
  if (h && *h < 100) *h = out->wlr_output->height;
}

int anim_scaled(int base, float scale_factor) {
  // subtle DPI scaling: multiply by scale but clamp
  float s = scale_factor;
  if (s < 1.0f) s = 1.0f;
  if (s > 2.0f) s = 2.0f;
  // responsive: base is designed for 400dp width
  return (int)(base * (0.85f + (s - 1.0f) * 0.35f));
}

int anim_scaled_icon(int base, float scale_factor) {
  return anim_scaled(base, scale_factor);
}

static int anim_tick(void *data) {
  struct anim_entry *e = data;
  e->elapsed += 16;
  if (e->elapsed >= e->duration) {
    if (e->is_x) wlr_scene_node_set_position(&e->tree->node, e->end_x, 0);
    else wlr_scene_node_set_position(&e->tree->node, 0, e->end_y);
    wl_event_source_remove(e->timer);
    e->timer = NULL;
    void (*done)(void*) = e->on_done;
    void *d = e->data;
    wl_list_remove(&e->link);
    free(e);
    if (done) done(d);
    return 0;
  }
  double t = (double)e->elapsed / (double)e->duration;
  double et = anim_ease_out_cubic(t);
  if (e->is_x) {
    int x = (int)(e->start_x + (e->end_x - e->start_x) * et);
    wlr_scene_node_set_position(&e->tree->node, x, 0);
  } else {
    int y = (int)(e->start_y + (e->end_y - e->start_y) * et);
    wlr_scene_node_set_position(&e->tree->node, 0, y);
  }
  wl_event_source_timer_update(e->timer, 16);
  return 0;
}

void anim_cancel(struct wlr_scene_tree *tree) {
  if (!tree) return;
  anims_init();
  struct anim_entry *e, *tmp;
  wl_list_for_each_safe(e, tmp, &active_anims, link) {
    if (e->tree == tree) {
      if (e->timer) wl_event_source_remove(e->timer);
      wl_list_remove(&e->link);
      free(e);
    }
  }
}

void anim_slide_y(struct phone_server *server, struct wlr_scene_tree *tree,
                  int from_y, int to_y, int duration_ms,
                  void (*on_done)(void *data), void *data) {
  if (!server || !tree) { if (on_done) on_done(data); return; }
  anims_init(); anim_cancel(tree);
  if (from_y == to_y) { wlr_scene_node_set_position(&tree->node, 0, to_y); if (on_done) on_done(data); return; }
  struct anim_entry *e = calloc(1, sizeof(*e));
  if (!e) { wlr_scene_node_set_position(&tree->node, 0, to_y); if (on_done) on_done(data); return; }
  e->tree=tree; e->start_y=from_y; e->end_y=to_y; e->is_x=false;
  e->duration=duration_ms>0?duration_ms:ANIM_DURATION_MS; e->elapsed=0; e->server=server; e->on_done=on_done; e->data=data;
  wlr_scene_node_set_position(&tree->node, 0, from_y);
  wl_list_insert(&active_anims, &e->link);
  e->timer=wl_event_loop_add_timer(server->event_loop, anim_tick, e);
  if(e->timer) wl_event_source_timer_update(e->timer,16); else { wlr_scene_node_set_position(&tree->node,0,to_y); wl_list_remove(&e->link); free(e); if(on_done) on_done(data); }
}
void anim_slide_x(struct phone_server *server, struct wlr_scene_tree *tree,
                  int from_x, int to_x, int duration_ms,
                  void (*on_done)(void *data), void *data) {
  if (!server || !tree) { if (on_done) on_done(data); return; }
  anims_init(); anim_cancel(tree);
  if (from_x == to_x) { wlr_scene_node_set_position(&tree->node, to_x, 0); if (on_done) on_done(data); return; }
  struct anim_entry *e = calloc(1, sizeof(*e));
  if (!e) { wlr_scene_node_set_position(&tree->node, to_x, 0); if (on_done) on_done(data); return; }
  e->tree=tree; e->start_x=from_x; e->end_x=to_x; e->is_x=true;
  e->duration=duration_ms>0?duration_ms:ANIM_DURATION_MS; e->elapsed=0; e->server=server; e->on_done=on_done; e->data=data;
  wlr_scene_node_set_position(&tree->node, from_x, 0);
  wl_list_insert(&active_anims, &e->link);
  e->timer=wl_event_loop_add_timer(server->event_loop, anim_tick, e);
  if(e->timer) wl_event_source_timer_update(e->timer,16); else { wlr_scene_node_set_position(&tree->node,to_x,0); wl_list_remove(&e->link); free(e); if(on_done) on_done(data); }
}
