#include "server.h"
#include "render.h"
#include "theme.h"
#include "anim.h"
#include <drm_fourcc.h>
#include <stdlib.h>
#include <wlr/render/allocator.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>

enum bar_button {
  BAR_BUTTON_BACK,
  BAR_BUTTON_HOME,
  BAR_BUTTON_RECENTS,
  BAR_BUTTON_KEYBOARD,
  BAR_BUTTON_MOUSE,
  BAR_BUTTON_COUNT,
};

// Navigation-Bar — iPhone-like floating pill + glass capsule
void bar_init(struct phone_server *server) {
  struct phone_bar *bar = calloc(1, sizeof(struct phone_bar));
  if (!bar) return;
  server->bar = bar;
  bar->server = server;
  bar->height = 44;

  // Floating glass pill instead of full-width bar
  float bg_r = 0.12f, bg_g = 0.12f, bg_b = 0.14f;
  if (server->theme) {
    bg_r = server->theme->bar_bg[0];
    bg_g = server->theme->bar_bg[1];
    bg_b = server->theme->bar_bg[2];
  }
  // keep a thin rect as fallback; will be replaced by pill in bar_update
  bar->bg_rect = wlr_scene_rect_create(
      server->layer_top_tree, 1, 1,
      (float[4]){bg_r, bg_g, bg_b, 0.97f});

  // Slightly larger, crisper icons (SF-Symbol style)
  bar->btn_back_label = phone_render_icon(server->allocator,
      PHONE_ICON_BACK, 26, 26, 1.0f, 1.0f, 1.0f, 0.96f);
  bar->btn_home_label = phone_render_icon(server->allocator,
      PHONE_ICON_HOME, 26, 26, 1.0f, 1.0f, 1.0f, 0.96f);
  bar->btn_recents_label = phone_render_icon(server->allocator,
      PHONE_ICON_RECENTS, 26, 26, 1.0f, 1.0f, 1.0f, 0.96f);
  bar->btn_keyboard_label = phone_render_icon(server->allocator,
      PHONE_ICON_KEYBOARD, 22, 22, 1.0f, 1.0f, 1.0f, 0.75f);
  bar->btn_mouse_label = phone_render_icon(server->allocator,
      PHONE_ICON_MOUSE, 22, 22, 1.0f, 1.0f, 1.0f, 0.75f);

  bar->btn_back_buf = bar->btn_back_label ? wlr_scene_buffer_create(
      server->layer_top_tree, bar->btn_back_label) : NULL;
  bar->btn_home_buf = bar->btn_home_label ? wlr_scene_buffer_create(
      server->layer_top_tree, bar->btn_home_label) : NULL;
  bar->btn_recents_buf = bar->btn_recents_label ? wlr_scene_buffer_create(
      server->layer_top_tree, bar->btn_recents_label) : NULL;
  bar->btn_keyboard_buf = bar->btn_keyboard_label ? wlr_scene_buffer_create(
      server->layer_top_tree, bar->btn_keyboard_label) : NULL;
  bar->btn_mouse_buf = bar->btn_mouse_label ? wlr_scene_buffer_create(
      server->layer_top_tree, bar->btn_mouse_label) : NULL;

  for (struct wlr_buffer **lbl = (struct wlr_buffer *[]){
           bar->btn_back_label, bar->btn_home_label, bar->btn_recents_label,
           bar->btn_keyboard_label, bar->btn_mouse_label, NULL };
       *lbl; lbl++) {
    wlr_buffer_drop(*lbl);
  }
}

// Navigation-Bar — floating pill centered at bottom (home indicator + 5 buttons)
void bar_update(struct phone_bar *bar) {
  if (!bar) return;
  struct phone_output *output;
  int sw, sh; float sc; anim_get_logical_size(bar->server, &sw, &sh, &sc);
  wl_list_for_each(output, &bar->server->outputs, link) {
    (void)output;
    int bh = bar->height; // 44 pill + 8 margin

    // Floating pill geometry: 320 x 44 centered, 10px above edge
    int pill_w = sw < 420 ? sw - 24 : 360;
    int pill_h = 44;
    int pill_x = (sw - pill_w) / 2;
    int pill_y = sh - pill_h - 10;
    // recolor with current theme
    if (bar->server->theme) {
      float *c = bar->server->theme->bar_bg;
      wlr_scene_rect_set_color(bar->bg_rect, (float[4]){c[0], c[1], c[2], 0.97f});
    }
    wlr_scene_node_set_position(&bar->bg_rect->node, pill_x, pill_y);
    wlr_scene_rect_set_size(bar->bg_rect, pill_w, pill_h);
    // Home indicator thin line centered at very bottom (like iPhone)
    // we keep bg_rect as pill; indicator is rendered via accent line: reuse keyboard buf pos if needed

    // Layout: 5 buttons evenly inside pill, slightly smaller hitboxes with padding
    int pad = 10;
    int avail = pill_w - 2*pad;
    int gap = avail / BAR_BUTTON_COUNT;
    int btn_y = pill_y + (pill_h - 26)/2;

    struct wlr_scene_buffer *bufs[BAR_BUTTON_COUNT] = {
        bar->btn_back_buf, bar->btn_home_buf, bar->btn_recents_buf,
        bar->btn_keyboard_buf, bar->btn_mouse_buf,
    };
    int *xs[BAR_BUTTON_COUNT] = {
        &bar->btn_back_x, &bar->btn_home_x, &bar->btn_recents_x,
        &bar->btn_keyboard_x, &bar->btn_mouse_x,
    };
    int *ys[BAR_BUTTON_COUNT] = {
        &bar->btn_back_y, &bar->btn_home_y, &bar->btn_recents_y,
        &bar->btn_keyboard_y, &bar->btn_mouse_y,
    };
    int *ws[BAR_BUTTON_COUNT] = {
        &bar->btn_back_w, &bar->btn_home_w, &bar->btn_recents_w,
        &bar->btn_keyboard_w, &bar->btn_mouse_w,
    };
    int *hs[BAR_BUTTON_COUNT] = {
        &bar->btn_back_h, &bar->btn_home_h, &bar->btn_recents_h,
        &bar->btn_keyboard_h, &bar->btn_mouse_h,
    };

    for (int i = 0; i < BAR_BUTTON_COUNT; i++) {
      if (!bufs[i]) continue;
      int sz = (i < 3) ? 26 : 22;
      int bx = pill_x + pad + i*gap + (gap - sz)/2;
      int by = btn_y + (26 - sz)/2;
      wlr_scene_node_set_position(&bufs[i]->node, bx, by);
      // enlarged hitbox (32x38) centered on icon for easier tapping
      *xs[i] = bx - 6;
      *ys[i] = by - 6;
      *ws[i] = sz + 12;
      *hs[i] = sz + 12;
    }
    break;
  }
}

// Prüfen ob ein Tap auf einen der Buttons gelandet ist
int bar_check_tap(struct phone_bar *bar, double x, double y) {
  if (!bar) return -1;

  // Buttons über ein zentrales Array prüfen
  struct wlr_scene_buffer *bufs[BAR_BUTTON_COUNT] = {
      bar->btn_back_buf, bar->btn_home_buf, bar->btn_recents_buf,
      bar->btn_keyboard_buf, bar->btn_mouse_buf,
  };
  int xs[BAR_BUTTON_COUNT] = {
      bar->btn_back_x, bar->btn_home_x, bar->btn_recents_x,
      bar->btn_keyboard_x, bar->btn_mouse_x,
  };
  int ys[BAR_BUTTON_COUNT] = {
      bar->btn_back_y, bar->btn_home_y, bar->btn_recents_y,
      bar->btn_keyboard_y, bar->btn_mouse_y,
  };
  int ws[BAR_BUTTON_COUNT] = {
      bar->btn_back_w, bar->btn_home_w, bar->btn_recents_w,
      bar->btn_keyboard_w, bar->btn_mouse_w,
  };
  int hs[BAR_BUTTON_COUNT] = {
      bar->btn_back_h, bar->btn_home_h, bar->btn_recents_h,
      bar->btn_keyboard_h, bar->btn_mouse_h,
  };

  for (int i = 0; i < BAR_BUTTON_COUNT; i++) {
    // Kollision mit dem jeweiligen Button prüfen
    if (bufs[i] &&
        x >= xs[i] && x < xs[i] + ws[i] &&
        y >= ys[i] && y < ys[i] + hs[i]) {
      return i;
    }
  }
  // Kein Button getroffen
  return -1;
}
