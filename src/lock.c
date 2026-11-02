#define _POSIX_C_SOURCE 200809L
#include "lock.h"
#include "notify.h"
#include "render.h"
#include "server.h"
#include "theme.h"
#include "anim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wlr/render/allocator.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

// Akkustand für die Sperrbildschirm-Anzeige lesen (einfache Variante)
static void lock_battery(char *buf, size_t len) {
  char cap[16] = "?";
  const char *bases[] = {
    "/sys/class/power_supply/BAT0",
    "/sys/class/power_supply/BAT1",
    "/sys/class/power_supply/battery",
    NULL,
  };
  for (int i = 0; bases[i]; i++) {
    char path[128];
    snprintf(path, sizeof(path), "%s/capacity", bases[i]);
    FILE *f = fopen(path, "r");
    if (f) {
      size_t n = fread(cap, 1, sizeof(cap) - 1, f);
      fclose(f);
      if (n > 0) { while (n > 0 && (cap[n-1]=='\n'||cap[n-1]=='\r')) cap[--n]=0; cap[n]=0; break; }
    }
  }
  if (cap[0] == '?') snprintf(buf, len, "%s", "AC");
  else snprintf(buf, len, "\u25A0 %s%%", cap);
}

// Sperrbildschirm initialisieren
void lock_init(struct phone_server *server) {
  struct phone_lock *lk = calloc(1, sizeof(struct phone_lock));
  if (!lk) return;
  server->lock = lk;
  lk->server = server;
  lk->visible = false;

  lk->tree = wlr_scene_tree_create(server->layer_overlay_tree);
  lk->tree->node.data = lk;

  wlr_scene_node_set_enabled(&lk->tree->node, false);
  wlr_log(WLR_INFO, "%s", "Lock screen initialized");
}

// Sperrbildschirm anzeigen
static void lock_hide_done(void *d){ struct phone_lock *lk=d; if(lk && !lk->visible) wlr_scene_node_set_enabled(&lk->tree->node,false); }
void lock_show(struct phone_lock *lk) {
  if (!lk || lk->visible) return;
  struct phone_server *server = lk->server;

  // Andere Overlays schließen
  if (server->control_center && server->control_center->visible)
    control_center_hide(server->control_center);
  if (server->recents && server->recents->visible)
    recents_hide(server->recents);
  if (server->launcher && server->launcher->visible)
    launcher_hide(server->launcher);
  if (server->notify && server->notify->visible)
    notify_hide(server->notify);

  int sw, sh; float sc; anim_get_logical_size(server, &sw, &sh, &sc);

  // Vorherige Nodes entfernen
  if (lk->bg_rect) { wlr_scene_node_destroy(&lk->bg_rect->node); lk->bg_rect = NULL; }
  if (lk->clock_buf) { wlr_scene_node_destroy(&lk->clock_buf->node); lk->clock_buf = NULL; }
  if (lk->hint_buf) { wlr_scene_node_destroy(&lk->hint_buf->node); lk->hint_buf = NULL; }
  if (lk->bat_buf) { wlr_scene_node_destroy(&lk->bat_buf->node); lk->bat_buf = NULL; }

  // Hintergrund: thematisches Wallpaper + 0.42 Abdunkelung (wie iOS)
  struct wlr_buffer *bg = phone_render_gradient(server->allocator,
      sw, sh,
      server->theme ? server->theme->wallpaper_start[0] : 0.08f,
      server->theme ? server->theme->wallpaper_start[1] : 0.12f,
      server->theme ? server->theme->wallpaper_start[2] : 0.32f,
      server->theme ? server->theme->wallpaper_end[0] : 0.02f,
      server->theme ? server->theme->wallpaper_end[1] : 0.02f,
      server->theme ? server->theme->wallpaper_end[2] : 0.08f);
  lk->bg_rect = wlr_scene_rect_create(lk->tree, sw, sh, (float[4]){0,0,0,0.42f});
  if (bg) {
    struct wlr_scene_buffer *gb = wlr_scene_buffer_create(lk->tree, bg);
    wlr_scene_node_set_position(&gb->node, 0, 0);
    wlr_buffer_drop(bg);
  }

  // Datum oben (wie iOS)
  time_t now = time(NULL);
  struct tm *tm = localtime(&now);
  char date_str[48];
  strftime(date_str, sizeof(date_str), "%A, %d. %B", tm);
  struct wlr_buffer *db = phone_render_text(server->allocator, date_str, 360, 28, 14, 0.96f,0.96f,0.98f);
  if (db) {
    struct wlr_scene_buffer *dbuf = wlr_scene_buffer_create(lk->tree, db);
    wlr_scene_node_set_position(&dbuf->node, (sw - 360)/2, 56);
    wlr_buffer_drop(db);
  }
  // Große Uhr — sehr leicht, 82pt
  char clock_str[16];
  strftime(clock_str, sizeof(clock_str), "%H:%M", tm);
  struct wlr_buffer *cb = phone_render_text(server->allocator, clock_str, 420, 130, 82, 1.0f, 1.0f, 1.0f);
  if (cb) {
    lk->clock_buf = wlr_scene_buffer_create(lk->tree, cb);
    wlr_scene_node_set_position(&lk->clock_buf->node, (sw - 420)/2, 78);
    wlr_buffer_drop(cb);
  }
  char batt[32]; lock_battery(batt, sizeof(batt));
  struct wlr_buffer *btb = phone_render_text(server->allocator, batt, 180, 28, 13, 1,1,1);
  if (btb) {
    lk->bat_buf = wlr_scene_buffer_create(lk->tree, btb);
    wlr_scene_node_set_position(&lk->bat_buf->node, (sw - 180)/2, 210);
    wlr_buffer_drop(btb);
  }
  // Hint + home indicator + fake flashlight/camera pills unten
  struct wlr_buffer *hb = phone_render_text(server->allocator, "\u2303  Zum Entsperren hochwischen", 340, 32, 13, 0.96f,0.96f,0.98f);
  if (hb) {
    lk->hint_buf = wlr_scene_buffer_create(lk->tree, hb);
    wlr_scene_node_set_position(&lk->hint_buf->node, (sw - 340)/2, sh - 96);
    wlr_buffer_drop(hb);
  }
  // bottom pills — Taschenlampe & Kamera (dekor)
  for (int i=0;i<2;i++) {
    int bx = i==0 ? 28 : sw-28-52;
    int by = sh - 52 - 18;
    struct wlr_buffer *pill = phone_render_pill(server->allocator, 52, 52, 0.18f,0.18f,0.20f,0.86f);
    if (pill) {
      struct wlr_scene_buffer *pb = wlr_scene_buffer_create(lk->tree, pill);
      wlr_scene_node_set_position(&pb->node, bx, by);
      wlr_buffer_drop(pill);
    }
    const char *g = i==0 ? "\u2600" : "\u25CB";
    struct wlr_buffer *gb2 = phone_render_text(server->allocator, g, 52, 52, 18, 1,1,1);
    if (gb2) { struct wlr_scene_buffer *tb = wlr_scene_buffer_create(lk->tree, gb2); wlr_scene_node_set_position(&tb->node, bx, by); wlr_buffer_drop(gb2); }
  }
  // home indicator
  { struct wlr_buffer *ind = phone_render_pill(server->allocator, 134, 5, 1,1,1,0.92f); if (ind){ struct wlr_scene_buffer *ib = wlr_scene_buffer_create(lk->tree, ind); wlr_scene_node_set_position(&ib->node,(sw-134)/2, sh-8); wlr_buffer_drop(ind);} }

  wlr_scene_node_set_enabled(&lk->tree->node, true);
  lk->visible = true;
  anim_cancel(lk->tree);
  anim_slide_y(server, lk->tree, sh, 0, ANIM_DURATION_LONG, NULL, NULL);
  wlr_log(WLR_INFO, "%s", "Lock screen ON");
}

void lock_hide(struct phone_lock *lk) {
  if (!lk || !lk->visible) return;
  lk->visible = false;
  int sw2, sh2; float sc2; anim_get_logical_size(lk->server, &sw2, &sh2, &sc2);
  anim_slide_y(lk->server, lk->tree, 0, sh2, ANIM_DURATION_MS, lock_hide_done, lk);
  wlr_log(WLR_INFO, "%s", "Lock screen OFF (unlocked)");
}

void lock_toggle(struct phone_lock *lk) {
  if (!lk) return;
  if (lk->visible) lock_hide(lk);
  else lock_show(lk);
}

// Tap auf dem Sperrbildschirm wird verbraucht (kein Durchreichen)
void lock_handle_tap(struct phone_lock *lk, double x, double y) {
  (void)x; (void)y;
  // Tap allein entsperrt nicht; nur ein Swipe nach oben entsperrt.
}