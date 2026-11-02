#define _POSIX_C_SOURCE 200809L
#include "power.h"
#include "notify.h"
#include "render.h"
#include "server.h"
#include "anim.h"
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wlr/render/allocator.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

static const char *power_labels[POWER_NUM_BUTTONS] = {
  "Ausschalten",
  "Neustart",
  "Ruhezustand",
};

static const char *power_commands[POWER_NUM_BUTTONS] = {
  "systemctl poweroff",
  "systemctl reboot",
  "systemctl suspend",
};

// Power-Menü initialisieren
void power_init(struct phone_server *server) {
  struct phone_power *pw = calloc(1, sizeof(struct phone_power));
  if (!pw) return;
  server->power = pw;
  pw->server = server;
  pw->visible = false;

  pw->tree = wlr_scene_tree_create(server->layer_overlay_tree);
  pw->tree->node.data = pw;

  wlr_scene_node_set_enabled(&pw->tree->node, false);
  wlr_log(WLR_INFO, "%s", "Power menu initialized");
}

// Systemkommando im Hintergrund starten
static void power_run(const char *cmd) {
  wlr_log(WLR_INFO, "power: executing '%s'", cmd);
  pid_t pid = fork();
  if (pid == 0) {
    setsid();
    execlp("/bin/sh", "sh", "-c", cmd, NULL);
    _exit(1);
  }
}

static void power_hide_done(void *data){ struct phone_power *pw=data; if(pw && !pw->visible) wlr_scene_node_set_enabled(&pw->tree->node,false); }
void power_show(struct phone_power *pw) {
  if (!pw || pw->visible) return;
  struct phone_server *server = pw->server;

  // Andere Overlays schließen
  if (server->control_center && server->control_center->visible)
    control_center_hide(server->control_center);
  if (server->recents && server->recents->visible)
    recents_hide(server->recents);
  if (server->launcher && server->launcher->visible)
    launcher_hide(server->launcher);
  if (server->notify && server->notify->visible)
    notify_hide(server->notify);

  // Vorherige Nodes entfernen falls vorhanden
  if (pw->bg_rect) { wlr_scene_node_destroy(&pw->bg_rect->node); pw->bg_rect = NULL; }
  for (int i = 0; i < POWER_NUM_BUTTONS; i++) {
    if (pw->btn_bg[i]) { wlr_scene_node_destroy(&pw->btn_bg[i]->node); pw->btn_bg[i] = NULL; }
    if (pw->btn_buf[i]) { wlr_scene_node_destroy(&pw->btn_buf[i]->node); pw->btn_buf[i] = NULL; }
  }

  int sw, sh; float sc; anim_get_logical_size(server, &sw, &sh, &sc);

  pw->bg_rect = wlr_scene_rect_create(pw->tree, sw, sh, (float[4]){0,0,0,0.48f});
  int bw = sw - 24;
  if (bw > 420) bw = 420;
  int bx = (sw - bw)/2;
  int bh = 56; int gap = 10;
  int total_h = POWER_NUM_BUTTONS * bh + (POWER_NUM_BUTTONS - 1)*gap + 18 + 12;
  int start_y = sh - total_h - 18;
  pw->btn_w = bw; pw->btn_h = bh; pw->btn_x = bx+8; pw->btn_y = start_y+18;
  // sheet glass behind
  {
    struct wlr_buffer *sheet = phone_render_glass(server->allocator, bw, total_h, 22, 0.16f,0.16f,0.18f,0.88f);
    if (sheet) { struct wlr_scene_buffer *sb = wlr_scene_buffer_create(pw->tree, sheet); wlr_scene_node_set_position(&sb->node, bx, start_y); wlr_buffer_drop(sheet); }
    struct wlr_buffer *handle = phone_render_pill(server->allocator, 40, 5, 1,1,1,0.28f);
    if (handle){ struct wlr_scene_buffer *hb = wlr_scene_buffer_create(pw->tree, handle); wlr_scene_node_set_position(&hb->node, (sw-40)/2, start_y+8); wlr_buffer_drop(handle); }
  }
  bw -= 16;
  for (int i = 0; i < POWER_NUM_BUTTONS; i++) {
    int by = pw->btn_y + i * (bh + gap);
    float cr=0.20f,cg=0.20f,cb=0.22f;
    if (i==0){ cr=0.96f; cg=0.26f; cb=0.26f; }
    pw->btn_bg[i] = wlr_scene_rect_create(pw->tree, bw, bh, (float[4]){cr,cg,cb,0.0f});
    // actual rounded pill overlay replaces rect visual
    struct wlr_buffer *pbg = phone_render_pill(server->allocator, bw, bh, cr,cg,cb,0.96f);
    if (pbg){ struct wlr_scene_buffer *pb = wlr_scene_buffer_create(pw->tree, pbg); wlr_scene_node_set_position(&pb->node, pw->btn_x, by); wlr_buffer_drop(pbg); wlr_scene_node_set_position(&pw->btn_bg[i]->node, pw->btn_x, by); wlr_scene_rect_set_size(pw->btn_bg[i], 0,0); }
    else { wlr_scene_node_set_position(&pw->btn_bg[i]->node, pw->btn_x, by); wlr_scene_rect_set_size(pw->btn_bg[i], bw, bh); }
    struct wlr_buffer *buf = phone_render_text(server->allocator, power_labels[i], bw, bh, 15, 1,1,1);
    if (buf){ pw->btn_buf[i]=wlr_scene_buffer_create(pw->tree, buf); wlr_scene_node_set_position(&pw->btn_buf[i]->node, pw->btn_x, by); wlr_buffer_drop(buf); }
  }

  wlr_scene_node_set_enabled(&pw->tree->node, true);
  pw->visible = true;
  anim_cancel(pw->tree);
  anim_slide_y(server, pw->tree, sh, 0, ANIM_DURATION_MS, NULL, NULL);
  wlr_log(WLR_INFO, "%s", "Power menu OPEN");
}

void power_hide(struct phone_power *pw) {
  if (!pw || !pw->visible) return;
  pw->visible = false;
  int sw2, sh2; float sc2; anim_get_logical_size(pw->server, &sw2, &sh2, &sc2);
  anim_slide_y(pw->server, pw->tree, 0, sh2, ANIM_DURATION_SHORT, power_hide_done, pw);
  wlr_log(WLR_INFO, "%s", "Power menu CLOSED");
}

void power_toggle(struct phone_power *pw) {
  if (!pw) return;
  if (pw->visible) power_hide(pw);
  else power_show(pw);
}

// Tap im Power-Menü verarbeiten
void power_handle_tap(struct phone_power *pw, double x, double y) {
  if (!pw || !pw->visible) return;

  int bw = pw->btn_w, bh = pw->btn_h, gap = 14;
  for (int i = 0; i < POWER_NUM_BUTTONS; i++) {
    int by = pw->btn_y + i * (bh + gap);
    if (x >= pw->btn_x && x < pw->btn_x + bw &&
        y >= by && y < by + bh) {
      power_run(power_commands[i]);
      power_hide(pw);
      return;
    }
  }

  // Tap außerhalb → schließen
  power_hide(pw);
}