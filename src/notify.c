#define _POSIX_C_SOURCE 200809L
#include "notify.h"
#include "render.h"
#include "server.h"
#include "anim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wlr/render/allocator.h>
#include <wlr/util/log.h>

// Alle Scene-Nodes des Shade zerstören
static void notify_clear(struct phone_notify *n) {
  if (n->bg_rect) {
    wlr_scene_node_destroy(&n->bg_rect->node);
    n->bg_rect = NULL;
  }
  if (n->header_buf) {
    wlr_scene_node_destroy(&n->header_buf->node);
    n->header_buf = NULL;
  }
}

// Benachrichtigungen initialisieren
void notify_init(struct phone_server *server) {
  struct phone_notify *n = calloc(1, sizeof(struct phone_notify));
  if (!n) return;
  server->notify = n;
  n->server = server;
  n->visible = false;
  n->count = 0;

  n->tree = wlr_scene_tree_create(server->layer_overlay_tree);
  n->tree->node.data = n;

  wlr_scene_node_set_enabled(&n->tree->node, false);
  wlr_log(WLR_INFO, "%s", "Notification shade initialized");
}

// Anzahl aktiver Benachrichtigungen
int notify_count(struct phone_notify *n) {
  if (!n) return 0;
  return n->count;
}

// Neue Benachrichtigung posten (älteste verdrängen)
void notify_post(struct phone_server *server,
                 const char *app, const char *title, const char *body) {
  struct phone_notify *n = server ? server->notify : NULL;
  if (!n) return;

  // Platz machen falls voll (älteste entfernen)
  if (n->count >= NOTIFY_MAX) {
    free(n->list[0].app);
    free(n->list[0].title);
    free(n->list[0].body);
    for (int i = 1; i < n->count; i++) n->list[i - 1] = n->list[i];
    n->count--;
  }

  struct phone_notification *msg = &n->list[n->count];
  msg->app = app && app[0] ? strdup(app) : strdup("System");
  msg->title = title && title[0] ? strdup(title) : strdup("");
  msg->body = body && body[0] ? strdup(body) : strdup("");
  msg->time = (uint32_t)time(NULL);
  n->count++;

  wlr_log(WLR_INFO, "notification: [%s] %s", msg->app, msg->title);
}

// Alle Benachrichtigungen löschen
void notify_clear_all(struct phone_notify *n) {
  if (!n) return;
  for (int i = 0; i < n->count; i++) {
    free(n->list[i].app);
    free(n->list[i].title);
    free(n->list[i].body);
  }
  n->count = 0;
}

// Shade öffnen: Dim + Kopfzeile + Karten aufbauen
static void notify_hide_done(void *d){ struct phone_notify *n=d; if(n && !n->visible) wlr_scene_node_set_enabled(&n->tree->node,false); }
void notify_show(struct phone_notify *n) {
  if (!n || n->visible) return;
  struct phone_server *server = n->server;

  // Andere Overlays schließen (Control Center, Recents, Launcher)
  if (server->control_center && server->control_center->visible)
    control_center_hide(server->control_center);
  if (server->recents && server->recents->visible)
    recents_hide(server->recents);
  if (server->launcher && server->launcher->visible)
    launcher_hide(server->launcher);

  int sw, sh; float sc; anim_get_logical_size(server, &sw, &sh, &sc);
  int sbar_h = server->statusbar ? server->statusbar->height : 0;

  notify_clear(n);

  // Vollflächiger Dim
  n->bg_rect = wlr_scene_rect_create(n->tree, sw, sh,
      (float[4]){0.0f, 0.0f, 0.0f, 0.55f});

  char head[64];
  if (n->count > 0) snprintf(head, sizeof(head), "Mitteilungen  \u2022 %d", n->count);
  else snprintf(head, sizeof(head), "%s", "Keine Mitteilungen");
  struct wlr_buffer *hb = phone_render_text(server->allocator, head, 260, 28, 13, 1,1,1);
  if (hb) {
    n->header_buf = wlr_scene_buffer_create(n->tree, hb);
    wlr_scene_node_set_position(&n->header_buf->node, (sw - 260)/2, sbar_h + 14);
    wlr_buffer_drop(hb);
  }

  // Karten für jede Benachrichtigung
  int card_w = sw * 92 / 100;
  int x = (sw - card_w) / 2;
  int y = sbar_h + 52;

  for (int i = 0; i < n->count; i++) {
    struct phone_notification *msg = &n->list[i];
    if (y + NOTIFY_CARD_H > sh) break;
    struct wlr_buffer *card_bg = phone_render_glass(server->allocator, card_w, NOTIFY_CARD_H, 18, 0.20f,0.20f,0.24f,0.90f);
    if (card_bg){ struct wlr_scene_buffer *cb = wlr_scene_buffer_create(n->tree, card_bg); wlr_scene_node_set_position(&cb->node, x, y); wlr_buffer_drop(card_bg); }
    else wlr_scene_rect_create(n->tree, card_w, NOTIFY_CARD_H, (float[4]){0.20f,0.20f,0.24f,0.90f});
    char line[256];
    if (msg->body && msg->body[0]) snprintf(line, sizeof(line), "\u25CF  %s — %s", msg->app, msg->title);
    else snprintf(line, sizeof(line), "\u25CF  %s%s%s", msg->app, msg->title[0] ? " — " : "", msg->title);
    struct wlr_buffer *tb = phone_render_text(server->allocator, line, card_w - 24, NOTIFY_CARD_H, 13, 0.98f,0.98f,0.99f);
    if (tb){ struct wlr_scene_buffer *sb = wlr_scene_buffer_create(n->tree, tb); wlr_scene_node_set_position(&sb->node, x + 12, y); wlr_buffer_drop(tb); }
    y += NOTIFY_CARD_H + 10;
  }

  if (n->count > 0) {
    // "Alles löschen" Button unten
    int btn_w = 160;
    int btn_h = 36;
    int bx = (sw - btn_w) / 2;
    int by = sh - btn_h - (server->bar ? server->bar->height : 0) - 12;
    wlr_scene_rect_create(n->tree, btn_w, btn_h,
        (float[4]){0.7f, 0.15f, 0.15f, 0.9f});
    struct wlr_buffer *bb = phone_render_text(server->allocator,
        "Alles loeschen", btn_w, btn_h, 13, 1.0f, 1.0f, 1.0f);
    if (bb) {
      struct wlr_scene_buffer *sb = wlr_scene_buffer_create(n->tree, bb);
      wlr_scene_node_set_position(&sb->node, bx, by);
      wlr_buffer_drop(bb);
    }
  }

  wlr_scene_node_set_enabled(&n->tree->node, true);
  n->visible = true;
  anim_cancel(n->tree);
  anim_slide_y(server, n->tree, -sh, 0, ANIM_DURATION_MS, NULL, NULL);
  wlr_log(WLR_INFO, "%s", "notification shade OPEN");
}

void notify_hide(struct phone_notify *n) {
  if (!n || !n->visible) return;
  n->visible = false;
  int sw2, sh2; float sc2; anim_get_logical_size(n->server, &sw2, &sh2, &sc2);
  anim_slide_y(n->server, n->tree, 0, -sh2, ANIM_DURATION_SHORT, notify_hide_done, n);
  wlr_log(WLR_INFO, "%s", "notification shade CLOSED");
}

void notify_toggle(struct phone_notify *n) {
  if (!n) return;
  if (n->visible) notify_hide(n);
  else notify_show(n);
}

// Tap im Shade: Tap auf "Alles löschen" leert die Liste, sonst schließen
void notify_handle_tap(struct phone_notify *n, double x, double y) {
  if (!n || !n->visible) return;

  struct phone_server *server = n->server;
  int sw = 800, sh = 480;
  struct phone_output *output = NULL;
  if (!wl_list_empty(&server->outputs)) {
    output = wl_container_of(server->outputs.next, output, link);
    sw = output->wlr_output->width;
    sh = output->wlr_output->height;
  }
  int sbar_h = server->statusbar ? server->statusbar->height : 0;

  // Kopfzeile antippen → alles löschen
  if (x >= (sw - 240) / 2 && x < (sw + 240) / 2 &&
      y >= sbar_h + 4 && y < sbar_h + 40) {
    notify_clear_all(n);
    notify_show(n); // neu rendern
    return;
  }

  // "Alles löschen"-Button unten antippen
  if (n->count > 0) {
    int btn_w = 160;
    int btn_h = 36;
    int bx = (sw - btn_w) / 2;
    int by = sh - btn_h - (server->bar ? server->bar->height : 0) - 12;
    if (x >= bx && x < bx + btn_w && y >= by && y < by + btn_h) {
      notify_clear_all(n);
      notify_show(n); // neu rendern
      return;
    }
  }

  // Standard: schließen (Shade ist ein Overlay, Tap drüber schließt)
  notify_hide(n);
}