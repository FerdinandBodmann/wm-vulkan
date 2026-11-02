#define _GNU_SOURCE
#include "server.h"
#include "anim.h"
#include "launcher.h"
#include "render.h"
#include <drm_fourcc.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/render/allocator.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

#define CLOSE_BTN_SIZE 36
#define ICON_SIZE 64
#define CARD_SPACING 12

// App-Icon fuer eine View laden (sucht in der Launcher-App-Liste)
static struct wlr_buffer *find_view_icon(struct phone_server *server,
    struct phone_view *view) {
  if (!server->launcher) return NULL;
  if (!view->xdg_surface ||
      view->xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL)
    return create_color_icon(0.4f, 0.4f, 0.4f, ICON_SIZE);

  // app_id ist die zuverlaessigste Quelle (z.B. "kitty", "org.gnome.Calculator")
  const char *app_id = view->xdg_surface->toplevel->app_id;
  const char *title = view->xdg_surface->toplevel->title;
  if (!app_id) app_id = "";
  if (!title) title = "";

  for (int i = 0; i < server->launcher->num_apps; i++) {
    struct phone_app *app = &server->launcher->apps[i];
    bool match = false;

    // 1) app_id gegen Name/Command pruefen (kuerzester Treffer gewinnt)
    if (app->name && app_id[0] && strcasestr(app_id, app->name))
      match = true;
    if (app->command && app_id[0]) {
      // Nur den Basisnamen des Commands vergleichen (ohne Argumente)
      const char *cmd = app->command;
      char buf[256];
      strncpy(buf, cmd, sizeof(buf) - 1);
      buf[sizeof(buf) - 1] = '\0';
      char *space = strchr(buf, ' ');
      if (space) *space = '\0';
      if (strcasestr(app_id, buf))
        match = true;
    }

    // 2) Fallback: Titel pruefen
    if (!match && app->name && title[0] && strcasestr(title, app->name))
      match = true;

    if (match) {
      char *found = find_icon(app->icon_path ? app->icon_path : app->name);
      if (found) {
        struct wlr_buffer *buf = load_icon_png(found, ICON_SIZE);
        free(found);
        if (buf) return buf;
      }
      return create_color_icon(app->color[0], app->color[1], app->color[2], ICON_SIZE);
    }
  }

  // Letzter Fallback: graues Icon
  return create_color_icon(0.4f, 0.4f, 0.4f, ICON_SIZE);
}

// Alle Karten aufraeumen
static void recents_clear_cards(struct phone_recents *recents) {
  for (int i = 0; i < recents->num_cards; i++) {
    struct phone_recents_card *card = &recents->cards[i];
    if (card->icon_buf) {
      wlr_scene_node_destroy(&card->icon_buf->node);
      card->icon_buf = NULL;
    }
    if (card->bg_rect) {
      wlr_scene_node_destroy(&card->bg_rect->node);
      card->bg_rect = NULL;
    }
    if (card->close_bg) {
      wlr_scene_node_destroy(&card->close_bg->node);
      card->close_bg = NULL;
    }
    if (card->close_buf) {
      wlr_scene_node_destroy(&card->close_buf->node);
      card->close_buf = NULL;
    }
    card->icon_wlr_buf = NULL;
    card->close_wlr_buf = NULL;
  }
  recents->num_cards = 0;

  if (recents->close_all_bg) {
    wlr_scene_node_destroy(&recents->close_all_bg->node);
    recents->close_all_bg = NULL;
  }
  if (recents->close_all_buf) {
    wlr_scene_node_destroy(&recents->close_all_buf->node);
    recents->close_all_buf = NULL;
  }
  recents->close_all_wlr_buf = NULL;
}

// Ein gemapptes View-Fenster zum Schließen auffordern
static void close_view(struct phone_view *view) {
  if (view && view->mapped && view->xdg_surface &&
      view->xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
    wlr_xdg_toplevel_send_close(view->xdg_surface->toplevel);
  }
}

// Recents-Übersicht initialisieren (Scene-Tree im Overlay anlegen)
void recents_init(struct phone_server *server) {
  struct phone_recents *recents = calloc(1, sizeof(struct phone_recents));
  if (!recents) return;
  server->recents = recents;
  recents->server = server;
  recents->visible = false;

  recents->tree = wlr_scene_tree_create(server->layer_overlay_tree);
  recents->tree->node.data = recents;

  recents->bg_rect = wlr_scene_rect_create(
      recents->tree, 1, 1,
      (float[4]){0.0f, 0.0f, 0.0f, 0.7f});
  wlr_scene_node_set_position(&recents->bg_rect->node, 0, 0);

  wlr_scene_node_set_enabled(&recents->tree->node, false);
}

// Recents-Übersicht anzeigen: Karten aller offenen Apps aufbauen
static void recents_hide_done(void *d){ struct phone_recents *r=d; if(r && !r->visible) wlr_scene_node_set_enabled(&r->tree->node,false); }
void recents_show(struct phone_recents *recents) {
  if (recents->visible) return;

  struct phone_server *server = recents->server;

  if (server->launcher && server->launcher->visible)
    launcher_hide(server->launcher);

  // Control Center schließen falls offen (Overlays ausschließen sich)
  if (server->control_center && server->control_center->visible)
    control_center_hide(server->control_center);

  int screen_w, screen_h; float sc; anim_get_logical_size(server, &screen_w, &screen_h, &sc);
  if (screen_w < 100) return;
  int bar_h = server->bar ? server->bar->height : 0;
  int sbar_h = server->statusbar ? server->statusbar->height : 0;
  int avail_h = screen_h - bar_h - sbar_h;

  wlr_scene_rect_set_size(recents->bg_rect, screen_w, screen_h);

  struct phone_view *views[RECENTS_MAX_CARDS];
  int num_views = 0;
  struct phone_workspace *ws;
  wl_list_for_each(ws, &server->workspaces, link) {
    struct phone_view *view;
    wl_list_for_each(view, &ws->views, link) {
      if (view->mapped && num_views < RECENTS_MAX_CARDS)
        views[num_views++] = view;
    }
  }

  if (num_views == 0) {
    wlr_scene_node_set_enabled(&recents->tree->node, true);
    recents->visible = true;
    return;
  }

  int card_w = screen_w * 80 / 100;
  int card_h = ICON_SIZE + 24;

  int max_visible = (avail_h - 80) / (card_h + CARD_SPACING);
  if (max_visible < 1) max_visible = 1;
  if (num_views > max_visible) num_views = max_visible;

  int total_h = num_views * (card_h + CARD_SPACING) - CARD_SPACING;
  int start_y = sbar_h + (avail_h - total_h - 60) / 2;

  for (int i = 0; i < num_views; i++) {
    struct phone_recents_card *card = &recents->cards[i];
    card->view = views[i];
    card->w = card_w;
    card->h = card_h;
    card->x = (screen_w - card_w) / 2;
    card->y = start_y + i * (card_h + CARD_SPACING);

    card->bg_rect = wlr_scene_rect_create(recents->tree, card_w, card_h, (float[4]){0.22f,0.22f,0.26f,0.0f});
    { struct wlr_buffer *cbg = phone_render_glass(server->allocator, card_w, card_h, 22, 0.22f,0.22f,0.26f,0.92f); if(cbg){ struct wlr_scene_buffer *cb = wlr_scene_buffer_create(recents->tree,cbg); wlr_scene_node_set_position(&cb->node, card->x, card->y); wlr_buffer_drop(cbg);} }
    wlr_scene_node_set_position(&card->bg_rect->node, card->x, card->y);

    int icon_x = card->x + 12;
    int icon_y = card->y + (card_h - ICON_SIZE) / 2;
    card->icon_wlr_buf = find_view_icon(server, views[i]);
    if (card->icon_wlr_buf) {
      card->icon_buf = wlr_scene_buffer_create(recents->tree, card->icon_wlr_buf);
      wlr_scene_buffer_set_source_box(card->icon_buf,
          &(struct wlr_fbox){0, 0, ICON_SIZE, ICON_SIZE});
      wlr_buffer_drop(card->icon_wlr_buf);
      card->icon_wlr_buf = NULL;
      wlr_scene_node_set_position(&card->icon_buf->node, icon_x, icon_y);
    }

    card->close_w = CLOSE_BTN_SIZE;
    card->close_h = CLOSE_BTN_SIZE;
    card->close_x = card->x + card_w - CLOSE_BTN_SIZE - 10;
    card->close_y = card->y + (card_h - CLOSE_BTN_SIZE) / 2;

    card->close_bg = wlr_scene_rect_create(
        recents->tree, CLOSE_BTN_SIZE, CLOSE_BTN_SIZE,
        (float[4]){0.8f, 0.2f, 0.2f, 0.9f});
    wlr_scene_node_set_position(&card->close_bg->node, card->close_x, card->close_y);

    card->close_wlr_buf = phone_render_text(
        server->allocator, "X", CLOSE_BTN_SIZE, CLOSE_BTN_SIZE, 18, 1.0, 1.0, 1.0);
    if (card->close_wlr_buf) {
      card->close_buf = wlr_scene_buffer_create(recents->tree, card->close_wlr_buf);
      wlr_buffer_drop(card->close_wlr_buf);
      card->close_wlr_buf = NULL;
      wlr_scene_node_set_position(&card->close_buf->node, card->close_x, card->close_y);
    }
  }

  recents->num_cards = num_views;

  int btn_w = 180;
  int btn_h = 44;
  recents->close_all_w = btn_w;
  recents->close_all_h = btn_h;
  recents->close_all_x = (screen_w - btn_w) / 2;
  recents->close_all_y = start_y + total_h + 16;

  recents->close_all_bg = wlr_scene_rect_create(
      recents->tree, btn_w, btn_h,
      (float[4]){0.7f, 0.15f, 0.15f, 0.9f});
  wlr_scene_node_set_position(&recents->close_all_bg->node,
      recents->close_all_x, recents->close_all_y);

  recents->close_all_wlr_buf = phone_render_text(
      server->allocator, "Alle schliessen", btn_w, btn_h, 14, 1.0, 1.0, 1.0);
  if (recents->close_all_wlr_buf) {
    recents->close_all_buf = wlr_scene_buffer_create(
        recents->tree, recents->close_all_wlr_buf);
    wlr_buffer_drop(recents->close_all_wlr_buf);
    recents->close_all_wlr_buf = NULL;
    wlr_scene_node_set_position(&recents->close_all_buf->node,
        recents->close_all_x, recents->close_all_y);
  }

  wlr_scene_node_set_enabled(&recents->tree->node, true);
  recents->visible = true;
  anim_cancel(recents->tree);
  // slide up from bottom
  int sh2 = screen_h;
  anim_slide_y(server, recents->tree, sh2, 0, ANIM_DURATION_MS, NULL, NULL);
}

// Recents-Übersicht ausblenden
void recents_hide(struct phone_recents *recents) {
  if (!recents || !recents->visible) return;
  recents->visible = false;
  int sw2, sh2; float sc2; anim_get_logical_size(recents->server, &sw2, &sh2, &sc2);
  anim_slide_y(recents->server, recents->tree, 0, sh2, ANIM_DURATION_SHORT, recents_hide_done, recents);
}

// Tap in der Recents-Übersicht: Karte schließen oder App öffnen
void recents_handle_tap(struct phone_recents *recents, double x, double y) {
  if (!recents->visible) return;

  for (int i = 0; i < recents->num_cards; i++) {
    struct phone_recents_card *card = &recents->cards[i];

    if (x >= card->close_x && x < card->close_x + card->close_w &&
        y >= card->close_y && y < card->close_y + card->close_h) {
      close_view(card->view);
      if (card->icon_buf) { wlr_scene_node_destroy(&card->icon_buf->node); card->icon_buf = NULL; }
      if (card->bg_rect) { wlr_scene_node_destroy(&card->bg_rect->node); card->bg_rect = NULL; }
      if (card->close_bg) { wlr_scene_node_destroy(&card->close_bg->node); card->close_bg = NULL; }
      if (card->close_buf) { wlr_scene_node_destroy(&card->close_buf->node); card->close_buf = NULL; }
      for (int j = i; j < recents->num_cards - 1; j++)
        recents->cards[j] = recents->cards[j + 1];
      recents->num_cards--;
      i--;
      continue;
    }

    if (x >= card->x && x < card->x + card->w &&
        y >= card->y && y < card->y + card->h) {
      struct phone_view *view = card->view;
      if (view && view->mapped) {
        if (view->workspace->id != recents->server->current_workspace)
          workspace_switch_to(recents->server, view->workspace->id);
        focus_view(view, view->xdg_surface->surface);
      }
      recents_hide(recents);
      return;
    }
  }

  if (recents->close_all_bg &&
      x >= recents->close_all_x && x < recents->close_all_x + recents->close_all_w &&
      y >= recents->close_all_y && y < recents->close_all_y + recents->close_all_h) {
    for (int i = 0; i < recents->num_cards; i++)
      close_view(recents->cards[i].view);
    recents_hide(recents);
    return;
  }

  recents_hide(recents);
}
