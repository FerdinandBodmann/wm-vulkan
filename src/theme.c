#define _POSIX_C_SOURCE 200809L
#include "theme.h"
#include "server.h"
#include "statusbar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/util/log.h>

// Einzelne RGB-Werte aus "0.3,0.6,1.0" parsen
static bool parse_rgb(const char *str, float *r, float *g, float *b) {
  if (sscanf(str, "%f,%f,%f", r, g, b) == 3) return true;
  // Auch "#rrggbb" unterstuetzen
  unsigned int v;
  if (str[0] == '#' && sscanf(str + 1, "%6x", &v) == 1) {
    *r = ((v >> 16) & 0xFF) / 255.0f;
    *g = ((v >> 8) & 0xFF) / 255.0f;
    *b = (v & 0xFF) / 255.0f;
    return true;
  }
  return false;
}

// Farbstring ("r,g,b" oder "#rrggbb") in einen 4er-Array parsen
bool theme_parse_color(const char *str, float out[4]) {
  out[0] = 0.5f; out[1] = 0.5f; out[2] = 0.5f; out[3] = 1.0f;
  if (!str || !str[0]) return false;
  if (parse_rgb(str, &out[0], &out[1], &out[2])) {
    out[3] = 1.0f;
    return true;
  }
  return false;
}

// Dunkle Standardfarben — solid matte (Pixel/Material), no liquid glass
static void set_dark_defaults(struct phone_theme *t) {
  t->dark = true;
  memcpy(t->accent, (float[4]){0.38f, 0.62f, 1.00f, 1.0f}, sizeof(t->accent));
  memcpy(t->fg, (float[4]){0.98f, 0.98f, 0.99f, 1.0f}, sizeof(t->fg));
  memcpy(t->bar_bg, (float[4]){0.10f, 0.10f, 0.11f, 0.97f}, sizeof(t->bar_bg));
  memcpy(t->panel_bg, (float[4]){0.15f, 0.15f, 0.17f, 0.98f}, sizeof(t->panel_bg));
  memcpy(t->tile_inactive, (float[4]){0.24f, 0.24f, 0.27f, 1.0f}, sizeof(t->tile_inactive));
  memcpy(t->wallpaper_start, (float[4]){0.12f, 0.16f, 0.38f, 1.0f}, sizeof(t->wallpaper_start));
  memcpy(t->wallpaper_end, (float[4]){0.04f, 0.04f, 0.08f, 1.0f}, sizeof(t->wallpaper_end));
}

// Helle Standardfarben — solid matte
static void set_light_defaults(struct phone_theme *t) {
  t->dark = false;
  memcpy(t->accent, (float[4]){0.18f, 0.48f, 0.98f, 1.0f}, sizeof(t->accent));
  memcpy(t->fg, (float[4]){0.10f, 0.10f, 0.12f, 1.0f}, sizeof(t->fg));
  memcpy(t->bar_bg, (float[4]){0.97f, 0.97f, 0.98f, 0.97f}, sizeof(t->bar_bg));
  memcpy(t->panel_bg, (float[4]){0.95f, 0.95f, 0.97f, 0.98f}, sizeof(t->panel_bg));
  memcpy(t->tile_inactive, (float[4]){0.88f, 0.88f, 0.90f, 1.0f}, sizeof(t->tile_inactive));
  memcpy(t->wallpaper_start, (float[4]){0.68f, 0.78f, 0.98f, 1.0f}, sizeof(t->wallpaper_start));
  memcpy(t->wallpaper_end, (float[4]){0.92f, 0.94f, 0.98f, 1.0f}, sizeof(t->wallpaper_end));
}

// settings.conf einlesen (key=value, # Kommentare)
static void theme_load_file(struct phone_server *server, const char *path) {
  struct phone_theme *theme = server->theme;
  FILE *f = fopen(path, "r");
  if (!f) return;

  char line[512];
  while (fgets(line, sizeof(line), f)) {
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
      line[--len] = '\0';
    if (len == 0 || line[0] == '#') continue;

    char *eq = strchr(line, '=');
    if (!eq) continue;
    *eq = '\0';
    char *key = line;
    char *val = eq + 1;
    // Whitespace trimmen
    while (*val == ' ' || *val == '\t') val++;

    if (strcmp(key, "theme") == 0) {
      if (strncmp(val, "light", 5) == 0) {
        set_light_defaults(theme);
      } else if (strncmp(val, "dark", 4) == 0) {
        set_dark_defaults(theme);
      }
    } else if (strcmp(key, "accent") == 0) {
      theme_parse_color(val, theme->accent);
    } else if (strcmp(key, "wallpaper_start") == 0) {
      theme_parse_color(val, theme->wallpaper_start);
    } else if (strcmp(key, "wallpaper_end") == 0) {
      theme_parse_color(val, theme->wallpaper_end);
    } else if (strcmp(key, "lock_after") == 0) {
      // Auto-Sperre in Sekunden (0 = Aus)
      server->auto_lock_after = atoi(val);
      if (server->auto_lock_after < 0) server->auto_lock_after = 0;
    } else if (strcmp(key, "silent") == 0) {
      server->silent = strcmp(val, "true") == 0 || strcmp(val, "1") == 0;
    } else if (strcmp(key, "dnd") == 0) {
      server->dnd_enabled = strcmp(val, "true") == 0 || strcmp(val, "1") == 0;
    }
  }

  fclose(f);
  wlr_log(WLR_INFO, "Theme: %s mode loaded from %s",
      theme->dark ? "dark" : "light", path);
}

// Theme initialisieren: Defaults + Konfiguration einlesen
void theme_init(struct phone_server *server) {
  struct phone_theme *t = calloc(1, sizeof(struct phone_theme));
  if (!t) return;
  server->theme = t;

  set_dark_defaults(t);

  const char *config_home = getenv("XDG_CONFIG_HOME");
  char path[512];
  if (config_home && config_home[0])
    snprintf(path, sizeof(path), "%s/phone-wm/settings.conf", config_home);
  else {
    const char *home = getenv("HOME");
    if (home && home[0])
      snprintf(path, sizeof(path), "%s/.config/phone-wm/settings.conf", home);
    else {
      snprintf(path, sizeof(path), "/etc/phone-wm/settings.conf");
      theme_load_file(server, path);
      return;
    }
  }

  FILE *f = fopen(path, "r");
  if (f) {
    fclose(f);
    theme_load_file(server, path);
  } else {
    snprintf(path, sizeof(path), "/etc/phone-wm/settings.conf");
    theme_load_file(server, path);
  }
}

// Theme-Modus wechseln (Dunkel <-> Hell). Setzt die jeweiligen Defaults.
void theme_set_dark(struct phone_server *server, bool dark) {
  if (!server->theme) return;
  if (dark) set_dark_defaults(server->theme);
  else set_light_defaults(server->theme);
}

// Alle UI-Elemente live auf das aktuelle Theme umstellen
void theme_refresh_ui(struct phone_server *server) {
  struct phone_theme *t = server->theme;
  if (!t) return;

  if (server->bar && server->bar->bg_rect)
    wlr_scene_rect_set_color(server->bar->bg_rect, t->bar_bg);
  if (server->statusbar && server->statusbar->bg_rect) {
    wlr_scene_rect_set_color(server->statusbar->bg_rect, t->bar_bg);
    statusbar_update(server->statusbar);
  }
  wallpaper_update(server);
}