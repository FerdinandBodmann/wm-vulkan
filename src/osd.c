#define _POSIX_C_SOURCE 200809L
#include "osd.h"
#include "lock.h"
#include "power.h"
#include "server.h"
#include "render.h"
#include "theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#define OSD_HIDE_MS 1500 // OSD nach 1,5 Sekunden ausblenden

// OSD-Timer: Overlay ausblenden
static int osd_timeout(void *data) {
  struct phone_osd *osd = data;
  osd_hide(osd);
  return 0;
}

// OSD-System initialisieren
void osd_init(struct phone_server *server) {
  struct phone_osd *osd = calloc(1, sizeof(struct phone_osd));
  if (!osd) return;
  server->osd = osd;
  osd->server = server;

  // Im Overlay-Layer, über dem Control Center, unter Sperrbildschirm
  osd->tree = wlr_scene_tree_create(server->layer_overlay_tree);
  osd->tree->node.data = osd;
  wlr_scene_node_set_enabled(&osd->tree->node, false);

  wlr_log(WLR_INFO, "%s", "OSD initialized");
}

// OSD ausblenden und alle Scene-Nodes entfernen
void osd_hide(struct phone_osd *osd) {
  if (!osd || !osd->visible) return;
  if (osd->timer) {
    wl_event_source_remove(osd->timer);
    osd->timer = NULL;
  }
  if (osd->bg) {
    wlr_scene_node_destroy(&osd->bg->node);
    osd->bg = NULL;
  }
  if (osd->text) {
    wlr_scene_node_destroy(&osd->text->node);
    osd->text = NULL;
  }
  if (osd->track) {
    wlr_scene_node_destroy(&osd->track->node);
    osd->track = NULL;
  }
  if (osd->fill) {
    wlr_scene_node_destroy(&osd->fill->node);
    osd->fill = NULL;
  }
  if (osd->tree) wlr_scene_node_set_enabled(&osd->tree->node, false);
  osd->visible = false;
}

// OSD anzeigen (ersetzt den aktuellen), pct = 0..100
void osd_show(struct phone_server *server, const char *label, int pct) {
  if (!server || !server->osd) return;
  struct phone_osd *osd = server->osd;

  osd_hide(osd);

  // Nur über nicht-modalen Overlays anzeigen
  if (server->lock && server->lock->visible) return;
  if (server->power && server->power->visible) return;

  struct phone_output *output = NULL;
  if (!wl_list_empty(&server->outputs))
    output = wl_container_of(server->outputs.next, output, link);
  int sw = output ? output->wlr_output->width : 800;
  int sh = output ? output->wlr_output->height : 480;

  // Position: unten mittig, über der Navigationsleiste
  int bar_h = server->bar ? server->bar->height : 48;
  osd->w = 280;
  osd->h = 56;
  osd->x = (sw - osd->w) / 2;
  osd->y = sh - bar_h - osd->h - 14;

  struct phone_theme *theme = server->theme;
  // Glass pill like iOS volume HUD
  struct wlr_buffer *bg = phone_render_glass(server->allocator,
      osd->w, osd->h, osd->h / 2,
      theme->panel_bg[0], theme->panel_bg[1], theme->panel_bg[2], 0.88f);
  if (bg) {
    osd->bg = wlr_scene_buffer_create(osd->tree, bg);
    wlr_scene_node_set_position(&osd->bg->node, osd->x, osd->y);
    wlr_buffer_drop(bg);
  }

  // Beschriftung: "Label pct%"
  char txt[64];
  snprintf(txt, sizeof(txt), "%s %d%%", label, pct);
  struct wlr_buffer *tb = phone_render_text(server->allocator,
      txt, osd->w, 28, 14,
      theme->fg[0], theme->fg[1], theme->fg[2]);
  if (tb) {
    osd->text = wlr_scene_buffer_create(osd->tree, tb);
    wlr_scene_node_set_position(&osd->text->node, osd->x, osd->y + 6);
    wlr_buffer_drop(tb);
  }

  // Fortschrittsbalken (Track + Fill)
  int bar_w = osd->w - 32;
  int pbar_h = 6;
  int bar_x = osd->x + 16;
  int bar_y = osd->y + osd->h - 24;
  osd->track = wlr_scene_rect_create(osd->tree, bar_w, pbar_h,
      (float[4]){ 0.30f, 0.30f, 0.34f, 1.0f });
  wlr_scene_node_set_position(&osd->track->node, bar_x, bar_y);
  int fill_w = bar_w * pct / 100;
  if (fill_w < 1) fill_w = 1;
  if (fill_w > bar_w) fill_w = bar_w;
  osd->fill = wlr_scene_rect_create(osd->tree, fill_w, pbar_h,
      (float[4]){ theme->accent[0], theme->accent[1], theme->accent[2], 1.0f });
  wlr_scene_node_set_position(&osd->fill->node, bar_x, bar_y);

  wlr_scene_node_set_enabled(&osd->tree->node, true);
  osd->visible = true;

  // Timer für das Auto-Ausblenden starten
  osd->timer = wl_event_loop_add_timer(server->event_loop, osd_timeout, osd);
  if (osd->timer) wl_event_source_timer_update(osd->timer, OSD_HIDE_MS);
}