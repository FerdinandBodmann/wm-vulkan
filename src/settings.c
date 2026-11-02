#define _POSIX_C_SOURCE 200809L
#include "settings.h"
#include "controlcenter.h"
#include "launcher.h"
#include "lock.h"
#include "power.h"
#include "render.h"
#include "server.h"
#include "statusbar.h"
#include "theme.h"
#include "anim.h"
#include "toast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#define SETTINGS_MARGIN 24
#define SETTINGS_ROW_H 44
#define SETTINGS_ROW_GAP 6
#define SETTINGS_RADIUS 12

// Farbpaletten für die Einstellungen
static const float accent_colors[4][3] = {
  { 0.30f, 0.60f, 1.00f },
  { 0.25f, 0.80f, 0.45f },
  { 1.00f, 0.55f, 0.10f },
  { 0.65f, 0.40f, 1.00f },
};

// Wallpaper-Voreinstellungen (Startfarbe, Endfarbe)
static const float wp_colors[3][2][3] = {
  { { 0.05f, 0.08f, 0.20f }, { 0.02f, 0.02f, 0.05f } },
  { { 0.05f, 0.30f, 0.40f }, { 0.01f, 0.05f, 0.10f } },
  { { 0.95f, 0.70f, 0.35f }, { 0.40f, 0.20f, 0.05f } },
};

// Alle während des Aufbaus erzeugten Scene-Buffer freigeben
static void settings_clear_nodes(struct phone_settings *s) {
  if (s->bg_rect) {
    wlr_scene_node_destroy(&s->bg_rect->node);
    s->bg_rect = NULL;
  }
  if (s->panel_buf) {
    wlr_scene_node_destroy(&s->panel_buf->node);
    s->panel_buf = NULL;
  }
  if (s->title_buf) {
    wlr_scene_node_destroy(&s->title_buf->node);
    s->title_buf = NULL;
  }
  for (int i = 0; i < s->nodes_n; i++) {
    if (s->nodes[i])
      wlr_scene_node_destroy(&s->nodes[i]->node);
  }
  s->nodes_n = 0;
  s->items_n = 0;
}

// Einen Text-Scene-Buffer erzeugen und registrieren
static void settings_text(struct phone_settings *s,
    const char *text, int x, int y, int w, int h, int size,
    float r, float g, float b) {
  struct wlr_buffer *buf = phone_render_text(s->server->allocator,
      text, w, h, size, r, g, b);
  if (!buf) return;
  struct wlr_scene_buffer *sb = wlr_scene_buffer_create(s->tree, buf);
  wlr_scene_node_set_position(&sb->node, x, y);
  wlr_buffer_drop(buf);
  if (s->nodes_n < SETTINGS_MAX_NODES) s->nodes[s->nodes_n++] = sb;
}

// Abgerundete Zeile als Hintergrund erzeugen und registrieren
static void settings_row_bg(struct phone_settings *s,
    int x, int y, int w, int h, float r, float g, float b) {
  struct wlr_buffer *buf = phone_render_rounded_rect(s->server->allocator,
      w, h, SETTINGS_RADIUS, r, g, b, 1.0f);
  if (!buf) return;
  struct wlr_scene_buffer *sb = wlr_scene_buffer_create(s->tree, buf);
  wlr_scene_node_set_position(&sb->node, x, y);
  wlr_buffer_drop(buf);
  if (s->nodes_n < SETTINGS_MAX_NODES) s->nodes[s->nodes_n++] = sb;
}

// Tap-Ziel registrieren
static void settings_item(struct phone_settings *s, int id, int x, int y, int w, int h) {
  if (s->items_n >= SETTINGS_MAX_ITEMS) return;
  s->items[s->items_n].id = id;
  s->items[s->items_n].x = x;
  s->items[s->items_n].y = y;
  s->items[s->items_n].w = w;
  s->items[s->items_n].h = h;
  s->items_n++;
}

// Text rechtsbündig in einer Zeile anzeigen (Wert)
static void settings_row_value(struct phone_settings *s,
    const char *text, int row_y, int row_w) {
  struct phone_theme *t = s->server->theme;
  int vw = 120;
  settings_text(s, text, SETTINGS_MARGIN + row_w - vw - 8,
      row_y + (SETTINGS_ROW_H - 20) / 2, vw, 20, 12,
      t->accent[0], t->accent[1], t->accent[2]);
}

// Alle Inhalte neu aufbauen (nach jeder Änderung)
static void settings_render(struct phone_settings *s) {
  struct phone_server *server = s->server;
  struct phone_theme *theme = server->theme;

  settings_clear_nodes(s);

  int sw = s->screen_w;
  int sh = s->screen_h;

  int x = SETTINGS_MARGIN;
  int w = sw - 2 * SETTINGS_MARGIN;
  int y = STATUSBAR_HEIGHT + 10;

  // === Erste Berechnungspass: Alle Positionen durchrechnen, um panel_h zu kennen ===
  int content_end_y = y + 34; // Titel
  content_end_y += SETTINGS_ROW_H + SETTINGS_ROW_GAP; // Theme
  content_end_y += SETTINGS_ROW_H + SETTINGS_ROW_GAP; // Akzent
  content_end_y += SETTINGS_ROW_H + SETTINGS_ROW_GAP; // Wallpaper
  content_end_y += SETTINGS_ROW_H + SETTINGS_ROW_GAP; // Auto-Sperre
  content_end_y += SETTINGS_ROW_H + SETTINGS_ROW_GAP; // Nicht stoeren

  struct phone_launcher *launcher = server->launcher;
  int app_rows = launcher ? launcher->num_apps : 0;
  int apps_avail = sh - content_end_y - SETTINGS_ROW_H - (SETTINGS_ROW_H + SETTINGS_ROW_GAP) - 28;
  int max_app_rows = apps_avail / (SETTINGS_ROW_H + SETTINGS_ROW_GAP);
  if (app_rows > max_app_rows) app_rows = max_app_rows;
  if (app_rows < 0) app_rows = 0;

  if (app_rows > 0) {
    content_end_y += 24; // "Apps" Überschrift
    content_end_y += app_rows * (SETTINGS_ROW_H + SETTINGS_ROW_GAP);
  }
  content_end_y += SETTINGS_ROW_GAP; // Lücke vor Schließen
  content_end_y += SETTINGS_ROW_H; // Schließen-Button

  s->panel_h = content_end_y - STATUSBAR_HEIGHT - 10 + 12;
  if (s->panel_h > sh - STATUSBAR_HEIGHT - 8) s->panel_h = sh - STATUSBAR_HEIGHT - 8;

  // === Hintergrund (Dim) ===
  s->bg_rect = wlr_scene_rect_create(s->tree, sw, sh,
      (float[4]){ 0.0f, 0.0f, 0.0f, 0.55f });

  struct wlr_buffer *pb = phone_render_glass(server->allocator,
      sw, s->panel_h, 28,
      theme->panel_bg[0], theme->panel_bg[1], theme->panel_bg[2], 0.86f);
  if (pb) {
    s->panel_buf = wlr_scene_buffer_create(s->tree, pb);
    wlr_scene_node_set_position(&s->panel_buf->node, 0, STATUSBAR_HEIGHT + 2);
    wlr_buffer_drop(pb);
  }
  // handle
  { struct wlr_buffer *h = phone_render_pill(server->allocator, 44,5, 1,1,1,0.24f); if(h){ struct wlr_scene_buffer *hb=wlr_scene_buffer_create(s->tree,h); wlr_scene_node_set_position(&hb->node,(sw-44)/2, STATUSBAR_HEIGHT+8); wlr_buffer_drop(h);} }

  // === Ab hier: alles wird ÜBER dem Panel gerendert ===

  y = STATUSBAR_HEIGHT + 14;
  struct wlr_buffer *tb = phone_render_text(server->allocator,
      "Einstellungen", w, 32, 17,
      theme->fg[0], theme->fg[1], theme->fg[2]);
  if (tb) {
    s->title_buf = wlr_scene_buffer_create(s->tree, tb);
    wlr_scene_node_set_position(&s->title_buf->node, x, y);
    wlr_buffer_drop(tb);
  }
  y += 36;

  // Zeilen-Hintergrundfarbe
  float row_r = theme->tile_inactive[0];
  float row_g = theme->tile_inactive[1];
  float row_b = theme->tile_inactive[2];

  // Theme-Zeile
  settings_row_bg(s, x, y, w, SETTINGS_ROW_H, row_r, row_g, row_b);
  settings_item(s, SETTINGS_THEME, x, y, w, SETTINGS_ROW_H);
  settings_text(s, "\u263E  Theme", x + 14, y + (SETTINGS_ROW_H - 20) / 2, 140, 20, 12, theme->fg[0], theme->fg[1], theme->fg[2]);
  settings_row_value(s, theme->dark ? "Dunkel" : "Hell", y, w);
  y += SETTINGS_ROW_H + SETTINGS_ROW_GAP;

  // Akzent-Zeile mit 4 Farbfeldern
  settings_row_bg(s, x, y, w, SETTINGS_ROW_H, row_r, row_g, row_b);
  settings_text(s, "Akzent", x + 14, y + (SETTINGS_ROW_H - 20) / 2,
      120, 20, 12, theme->fg[0], theme->fg[1], theme->fg[2]);
  int swatch_size = 26;
  int swatch_gap = 6;
  int sw_x = x + w - 8 - 4 * (swatch_size + swatch_gap) + swatch_gap;
  for (int i = 0; i < 4; i++) {
    int sx = sw_x + i * (swatch_size + swatch_gap);
    int sy = y + (SETTINGS_ROW_H - swatch_size) / 2;
    bool active = (theme->accent[0] == accent_colors[i][0] &&
                   theme->accent[1] == accent_colors[i][1] &&
                   theme->accent[2] == accent_colors[i][2]);
    int draw = active ? swatch_size + 6 : swatch_size;
    struct wlr_buffer *sbuf = phone_render_rounded_rect(server->allocator,
        draw, draw, draw / 2,
        accent_colors[i][0], accent_colors[i][1], accent_colors[i][2], 1.0f);
    if (sbuf) {
      struct wlr_scene_buffer *sb2 = wlr_scene_buffer_create(s->tree, sbuf);
      wlr_scene_node_set_position(&sb2->node,
          sx - (draw - swatch_size) / 2, sy - (draw - swatch_size) / 2);
      wlr_buffer_drop(sbuf);
      if (s->nodes_n < SETTINGS_MAX_NODES) s->nodes[s->nodes_n++] = sb2;
    }
    settings_item(s, SETTINGS_ACCENT0 + i, sx, sy, swatch_size, swatch_size);
  }
  y += SETTINGS_ROW_H + SETTINGS_ROW_GAP;

  // Wallpaper-Zeile mit 3 Voreinstellungen
  settings_row_bg(s, x, y, w, SETTINGS_ROW_H, row_r, row_g, row_b);
  settings_text(s, "Wallpaper", x + 14, y + (SETTINGS_ROW_H - 20) / 2,
      120, 20, 12, theme->fg[0], theme->fg[1], theme->fg[2]);
  int wp_w = 40, wp_h = 18;
  int wp_x = x + w - 8 - 3 * (wp_w + 8);
  for (int i = 0; i < 3; i++) {
    int sx = wp_x + i * (wp_w + 8);
    int sy = y + (SETTINGS_ROW_H - wp_h) / 2;
    bool active = server->theme->wallpaper_start[0] == wp_colors[i][0][0] &&
                  server->theme->wallpaper_start[1] == wp_colors[i][0][1];
    struct wlr_buffer *sbuf = phone_render_rounded_rect(server->allocator,
        active ? wp_w + 6 : wp_w, active ? wp_h + 6 : wp_h, 6,
        wp_colors[i][0][0] * 0.6f, wp_colors[i][0][1] * 0.6f,
        wp_colors[i][0][2] * 0.6f, 1.0f);
    if (sbuf) {
      struct wlr_scene_buffer *sb2 = wlr_scene_buffer_create(s->tree, sbuf);
      wlr_scene_node_set_position(&sb2->node,
          sx - (active ? 3 : 0), sy - (active ? 3 : 0));
      wlr_buffer_drop(sbuf);
      if (s->nodes_n < SETTINGS_MAX_NODES) s->nodes[s->nodes_n++] = sb2;
    }
    settings_item(s, SETTINGS_WP0 + i, sx, sy, wp_w, wp_h);
  }
  y += SETTINGS_ROW_H + SETTINGS_ROW_GAP;

  // Auto-Sperre-Zeile
  settings_row_bg(s, x, y, w, SETTINGS_ROW_H, row_r, row_g, row_b);
  settings_item(s, SETTINGS_LOCK, x, y, w, SETTINGS_ROW_H);
  settings_text(s, "Auto-Sperre", x + 14, y + (SETTINGS_ROW_H - 20) / 2,
      120, 20, 12, theme->fg[0], theme->fg[1], theme->fg[2]);
  settings_row_value(s, server->auto_lock_after > 0 ? "An" : "Aus", y, w);
  y += SETTINGS_ROW_H + SETTINGS_ROW_GAP;

  // Nicht-stören-Zeile
  settings_row_bg(s, x, y, w, SETTINGS_ROW_H, row_r, row_g, row_b);
  settings_item(s, SETTINGS_DND, x, y, w, SETTINGS_ROW_H);
  settings_text(s, "Nicht stoeren", x + 14, y + (SETTINGS_ROW_H - 20) / 2,
      140, 20, 12, theme->fg[0], theme->fg[1], theme->fg[2]);
  settings_row_value(s, server->dnd_enabled ? "An" : "Aus", y, w);
  y += SETTINGS_ROW_H + SETTINGS_ROW_GAP;

  // Apps-Bereich: homescreen-Anzeige pro App umschalten
  if (app_rows > 0) {
    settings_text(s, "Apps", x + 4, y, 200, 20, 11,
        theme->fg[0], theme->fg[1], theme->fg[2]);
    y += 24;
    for (int i = 0; i < app_rows; i++) {
      struct phone_app *app = &launcher->apps[i];
      settings_row_bg(s, x, y, w, SETTINGS_ROW_H, row_r, row_g, row_b);
      settings_item(s, SETTINGS_APP_BASE + i, x, y, w, SETTINGS_ROW_H);
      settings_text(s, app->name ? app->name : "?", x + 14,
          y + (SETTINGS_ROW_H - 20) / 2, 160, 20, 12,
          theme->fg[0], theme->fg[1], theme->fg[2]);
      settings_row_value(s, app->home ? "Home" : "Aus", y, w);
      y += SETTINGS_ROW_H + SETTINGS_ROW_GAP;
    }
  }

  // Schließen-Button
  y += SETTINGS_ROW_GAP;
  settings_row_bg(s, x, y, w, SETTINGS_ROW_H,
      theme->accent[0], theme->accent[1], theme->accent[2]);
  settings_item(s, SETTINGS_CLOSE, x, y, w, SETTINGS_ROW_H);
  settings_text(s, "Schliessen", x + w / 2 - 60, y + (SETTINGS_ROW_H - 20) / 2,
      120, 20, 13,
      theme->fg[0], theme->fg[1], theme->fg[2]);
}

// settings.conf aus dem aktuellen Zustand schreiben
void settings_write(struct phone_settings *s) {
  struct phone_server *server = s->server;
  struct phone_theme *theme = server->theme;

  const char *config_home = getenv("XDG_CONFIG_HOME");
  char dir[512], path[512];
  if (config_home && config_home[0]) {
    snprintf(dir, sizeof(dir), "%s/phone-wm", config_home);
  } else {
    const char *home = getenv("HOME");
    if (!home || !home[0]) return;
    snprintf(dir, sizeof(dir), "%s/.config/phone-wm", home);
  }
  mkdir(dir, 0700);
  snprintf(path, sizeof(path), "%s/settings.conf", dir);

  FILE *f = fopen(path, "w");
  if (!f) return;

  fprintf(f, "# phone-wm Einstellungen\n");
  fprintf(f, "theme=%s\n", theme->dark ? "dark" : "light");
  fprintf(f, "accent=%.2f,%.2f,%.2f\n",
      theme->accent[0], theme->accent[1], theme->accent[2]);
  fprintf(f, "wallpaper_start=%.2f,%.2f,%.2f\n",
      theme->wallpaper_start[0], theme->wallpaper_start[1], theme->wallpaper_start[2]);
  fprintf(f, "wallpaper_end=%.2f,%.2f,%.2f\n",
      theme->wallpaper_end[0], theme->wallpaper_end[1], theme->wallpaper_end[2]);
  fprintf(f, "lock_after=%d\n", server->auto_lock_after);
  fprintf(f, "dnd=%s\n", server->dnd_enabled ? "true" : "false");
  fprintf(f, "silent=%s\n", server->silent ? "true" : "false");
  fclose(f);
  wlr_log(WLR_INFO, "Wrote settings to %s", path);
}

// Einstellungen-Seite initialisieren
void settings_init(struct phone_server *server) {
  struct phone_settings *s = calloc(1, sizeof(struct phone_settings));
  if (!s) return;
  server->settings = s;
  s->server = server;
  s->visible = false;

  s->tree = wlr_scene_tree_create(server->layer_overlay_tree);
  s->tree->node.data = s;
  wlr_scene_node_set_enabled(&s->tree->node, false);

  wlr_log(WLR_INFO, "%s", "Settings page initialized");
}

static void settings_hide_done(void *d){ struct phone_settings *s=d; if(s && !s->visible) wlr_scene_node_set_enabled(&s->tree->node,false); }
void settings_hide(struct phone_settings *s) {
  if (!s || !s->visible) return;
  s->visible = false;
  int sh2 = s->screen_h;
  anim_slide_y(s->server, s->tree, 0, sh2, ANIM_DURATION_SHORT, settings_hide_done, s);
}

// Seite öffnen bzw. nach einer Änderung neu aufbauen
void settings_show(struct phone_settings *s) {
  if (!s) return;
  struct phone_server *server = s->server;

  // Andere Overlays schließen
  if (server->launcher && server->launcher->visible)
    launcher_hide(server->launcher);
  if (server->recents && server->recents->visible)
    recents_hide(server->recents);
  if (server->control_center && server->control_center->visible)
    control_center_hide(server->control_center);

  int lw, lh; float lsc; anim_get_logical_size(server, &lw, &lh, &lsc);
  s->screen_w = lw; s->screen_h = lh;

  settings_render(s);
  wlr_scene_node_set_enabled(&s->tree->node, true);
  s->visible = true;
  anim_cancel(s->tree);
  int sh2 = s->screen_h;
  anim_slide_y(server, s->tree, sh2, 0, ANIM_DURATION_MS, NULL, NULL);
}

// Seite ein-/ausblenden (Umschalter)
void settings_toggle(struct phone_settings *s) {
  if (!s) return;
  if (s->visible) settings_hide(s);
  else settings_show(s);
}

// Tap auf der Seite verarbeiten
void settings_handle_tap(struct phone_settings *s, double x, double y) {
  if (!s || !s->visible) return;

  // Tap außerhalb des Panels → schließen
  if (y < STATUSBAR_HEIGHT + 2 || y > STATUSBAR_HEIGHT + 2 + s->panel_h) {
    settings_hide(s);
    return;
  }

  for (int i = 0; i < s->items_n; i++) {
    struct settings_item *it = &s->items[i];
    if (x < it->x || x >= it->x + it->w || y < it->y || y >= it->y + it->h)
      continue;

    struct phone_server *server = s->server;
    struct phone_theme *theme = server->theme;

    switch (it->id) {
    case SETTINGS_THEME: {
      bool was_dark = theme->dark;
      theme_set_dark(server, !was_dark);
      theme_refresh_ui(server);
      settings_write(s);
      break;
    }
    case SETTINGS_ACCENT0: case SETTINGS_ACCENT1:
    case SETTINGS_ACCENT2: case SETTINGS_ACCENT3: {
      int idx = it->id - SETTINGS_ACCENT0;
      theme->accent[0] = accent_colors[idx][0];
      theme->accent[1] = accent_colors[idx][1];
      theme->accent[2] = accent_colors[idx][2];
      theme_refresh_ui(server);
      settings_write(s);
      break;
    }
    case SETTINGS_WP0: case SETTINGS_WP1: case SETTINGS_WP2: {
      int idx = it->id - SETTINGS_WP0;
      const float (*c)[3] = wp_colors[idx];
      theme->wallpaper_start[0] = c[0][0];
      theme->wallpaper_start[1] = c[0][1];
      theme->wallpaper_start[2] = c[0][2];
      theme->wallpaper_end[0] = c[1][0];
      theme->wallpaper_end[1] = c[1][1];
      theme->wallpaper_end[2] = c[1][2];
      theme_refresh_ui(server);
      settings_write(s);
      break;
    }
    case SETTINGS_LOCK:
      server->auto_lock_after = server->auto_lock_after > 0 ? 0 : 60;
      server->last_input_time = time(NULL);
      settings_write(s);
      break;
    case SETTINGS_DND:
      server->dnd_enabled = !server->dnd_enabled;
      if (server->dnd_enabled && server->toast && server->toast->visible)
        toast_hide(server->toast);
      settings_write(s);
      break;
    case SETTINGS_CLOSE:
      settings_hide(s);
      return;
    default:
      // App-Zeile: homescreen-Anzeige umschalten
      if (it->id >= SETTINGS_APP_BASE && server->launcher) {
        int idx = it->id - SETTINGS_APP_BASE;
        if (idx < server->launcher->num_apps) {
          server->launcher->apps[idx].home = !server->launcher->apps[idx].home;
          launcher_write_config(server->launcher);
          if (server->homescreen) homescreen_update(server->homescreen);
        }
      }
    }
    break;
  }

  // Seite neu aufbauen (Werte haben sich geändert)
  settings_render(s);
}
