#define _POSIX_C_SOURCE 200809L
#include "toast.h"
#include "lock.h"
#include "power.h"
#include "server.h"
#include "render.h"
#include "statusbar.h"
#include "theme.h"
#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#define TOAST_HIDE_MS 3000 // Toast nach 3 Sekunden ausblenden

// Toast-Timer: Banner ausblenden
static int toast_timeout(void *data) {
  struct phone_toast *toast = data;
  toast_hide(toast);
  return 0;
}

// Toast-System initialisieren
void toast_init(struct phone_server *server) {
  struct phone_toast *toast = calloc(1, sizeof(struct phone_toast));
  if (!toast) return;
  server->toast = toast;
  toast->server = server;

  // Im Overlay-Layer, über allem (aber unter Sperrbildschirm & Power-Menü)
  toast->tree = wlr_scene_tree_create(server->layer_overlay_tree);
  toast->tree->node.data = toast;
  wlr_scene_node_set_enabled(&toast->tree->node, false);

  wlr_log(WLR_INFO, "%s", "Toast system initialized");
}

// Toast ausblenden und alle Scene-Nodes entfernen
void toast_hide(struct phone_toast *toast) {
  if (!toast || !toast->visible) return;
  if (toast->timer) {
    wl_event_source_remove(toast->timer);
    toast->timer = NULL;
  }
  if (toast->bg) {
    wlr_scene_node_destroy(&toast->bg->node);
    toast->bg = NULL;
  }
  if (toast->text) {
    wlr_scene_node_destroy(&toast->text->node);
    toast->text = NULL;
  }
  if (toast->tree) wlr_scene_node_set_enabled(&toast->tree->node, false);
  toast->visible = false;
}

// Toast anzeigen (ersetzt den aktuellen Toast)
void toast_show(struct phone_toast *toast, const char *app, const char *title) {
  if (!toast) return;
  struct phone_server *server = toast->server;

  // Nicht über Sperrbildschirm oder Power-Menü anzeigen
  if (server->lock && server->lock->visible) return;
  if (server->power && server->power->visible) return;

  toast_hide(toast);

  // Bildschirmgröße holen
  struct phone_output *output = NULL;
  if (!wl_list_empty(&server->outputs))
    output = wl_container_of(server->outputs.next, output, link);
  int sw = output ? output->wlr_output->width : 800;

  // Position: direkt unter der Statusleiste
  toast->w = sw - 24;
  toast->h = 44;
  toast->x = 12;
  toast->y = STATUSBAR_HEIGHT + 8;

  struct phone_theme *theme = server->theme;
  // Pillen-Toast wie iOS — glasig + weicher Schatten
  struct wlr_buffer *bg = phone_render_glass(server->allocator,
      toast->w, toast->h, 18,
      theme->panel_bg[0], theme->panel_bg[1], theme->panel_bg[2], 0.88f);
  if (bg) {
    toast->bg = wlr_scene_buffer_create(toast->tree, bg);
    wlr_scene_node_set_position(&toast->bg->node, toast->x, toast->y);
    wlr_buffer_drop(bg);
  }

  // Text: "App: Titel" mit Akzentfarbe für den App-Namen
  char label[160];
  if (app && app[0])
    snprintf(label, sizeof(label), "%s: %s", app, title ? title : "");
  else
    snprintf(label, sizeof(label), "%s", title ? title : "");
  struct wlr_buffer *tb = phone_render_text(server->allocator,
      label, toast->w - 24, toast->h, 13,
      theme->fg[0], theme->fg[1], theme->fg[2]);
  if (tb) {
    toast->text = wlr_scene_buffer_create(toast->tree, tb);
    wlr_scene_node_set_position(&toast->text->node, toast->x + 12, toast->y);
    wlr_buffer_drop(tb);
  }

  wlr_scene_node_set_enabled(&toast->tree->node, true);
  toast->visible = true;

  // Timer für das Auto-Ausblenden starten
  toast->timer = wl_event_loop_add_timer(server->event_loop, toast_timeout, toast);
  if (toast->timer) wl_event_source_timer_update(toast->timer, TOAST_HIDE_MS);
}