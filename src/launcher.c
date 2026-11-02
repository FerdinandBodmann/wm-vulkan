#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "server.h"
#include "render.h"
#include "theme.h"
#include "anim.h"
#include <cairo.h>
#include <ctype.h>
#include <drm_fourcc.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/render/allocator.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>
#include <glob.h>
#include <sys/stat.h>

static const struct phone_app default_apps[] = {
  { .name = "Terminal",   .command = "kitty",             .color = {0.1f, 0.1f, 0.1f} },
  { .name = "Browser",    .command = "firefox",          .color = {0.0f, 0.4f, 0.8f} },
  { .name = "Files",      .command = "thunar",           .color = {0.2f, 0.6f, 0.2f} },
  { .name = "Settings",   .command = "gnome-control-center", .color = {0.6f, 0.6f, 0.6f} },
  { .name = "Calculator", .command = "gnome-calculator", .color = {0.8f, 0.4f, 0.0f} },
  { .name = "Calendar",   .command = "gnome-calendar",   .color = {0.6f, 0.2f, 0.6f} },
  { .name = "Maps",       .command = "gnome-maps",       .color = {0.0f, 0.6f, 0.4f} },
  { .name = "Music",      .command = "spotify",          .color = {0.1f, 0.8f, 0.2f} },
};

// Pixel-Buffer fuer Cairo-Rendering (eigene wlr_buffer Implementierung)
struct icon_pixel_buf {
  struct wlr_buffer base;
  unsigned char *data;
  int stride;
};

// Icon-Pixel-Buffer zerstören und Speicher freigeben
static void icon_buf_destroy(struct wlr_buffer *buf) {
  struct icon_pixel_buf *b = wl_container_of(buf, b, base);
  free(b->data);
  free(b);
}

// Kein DMABUF für Icons unterstützt (nur CPU-Buffer)
static bool icon_buf_get_dmabuf(struct wlr_buffer *b, struct wlr_dmabuf_attributes *a) {
  (void)b; (void)a;
  return false;
}

// Kein SHM-Puffer für Icons unterstützt (nur CPU-Buffer)
static bool icon_buf_get_shm(struct wlr_buffer *b, struct wlr_shm_attributes *a) {
  (void)b; (void)a;
  return false;
}

// CPU-Zugriff auf die Icon-Pixeldaten erlauben (für Cairo)
static bool icon_buf_begin_access(struct wlr_buffer *buf, uint32_t flags,
    void **data, uint32_t *format, size_t *stride) {
  (void)flags;
  struct icon_pixel_buf *b = wl_container_of(buf, b, base);
  *data = b->data;
  *format = DRM_FORMAT_ARGB8888;
  *stride = b->stride;
  return true;
}

// CPU-Zugriff auf die Icon-Pixeldaten wieder freigeben
static void icon_buf_end_access(struct wlr_buffer *buf) { (void)buf; }

static const struct wlr_buffer_impl icon_buf_impl = {
  .destroy = icon_buf_destroy,
  .get_dmabuf = icon_buf_get_dmabuf,
  .get_shm = icon_buf_get_shm,
  .begin_data_ptr_access = icon_buf_begin_access,
  .end_data_ptr_access = icon_buf_end_access,
};

// PNG-Icon laden und in einen wlr_buffer umwandeln
struct wlr_buffer *load_icon_png(const char *path, int size) {
  cairo_surface_t *surf = cairo_image_surface_create_from_png(path);
  if (!surf || cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
    if (surf) cairo_surface_destroy(surf);
    return NULL;
  }

  int orig_w = cairo_image_surface_get_width(surf);
  int orig_h = cairo_image_surface_get_height(surf);

  // Skalieren auf quadratisches Icon
  cairo_surface_t *scaled = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
  cairo_t *cr = cairo_create(scaled);
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgba(cr, 0, 0, 0, 0);
  cairo_paint(cr);
  cairo_scale(cr, (double)size / orig_w, (double)size / orig_h);
  cairo_set_source_surface(cr, surf, 0, 0);
  cairo_paint(cr);
  cairo_destroy(cr);
  cairo_surface_destroy(surf);

  // Pixel-Buffer erstellen
  int stride = size * 4;
  struct icon_pixel_buf *buf = calloc(1, sizeof(struct icon_pixel_buf));
  if (!buf) { cairo_surface_destroy(scaled); return NULL; }

  buf->data = malloc(stride * size);
  if (!buf->data) { free(buf); cairo_surface_destroy(scaled); return NULL; }

  unsigned char *src = cairo_image_surface_get_data(scaled);
  memcpy(buf->data, src, stride * size);
  buf->stride = stride;

  wlr_buffer_init(&buf->base, &icon_buf_impl, size, size);
  cairo_surface_destroy(scaled);

  return &buf->base;
}

// Farbiges Icon — squircle (iOS) mit Gloss + Schatten + Initial-Buchstabe
struct wlr_buffer *create_color_icon(float r, float g, float b, int size) {
  int stride = size * 4;
  struct icon_pixel_buf *buf = calloc(1, sizeof(struct icon_pixel_buf));
  if (!buf) return NULL;
  buf->data = malloc(stride * size);
  if (!buf->data) { free(buf); return NULL; }
  buf->stride = stride;
  memset(buf->data, 0, stride * size);
  cairo_surface_t *surf = cairo_image_surface_create_for_data(buf->data, CAIRO_FORMAT_ARGB32, size, size, stride);
  cairo_t *cr = cairo_create(surf);
  double radius = size * 0.26; // squircle
  double x = 1.5, y = 1.5, w = size - 3, h = size - 3;
  // shadow
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - radius, y + radius + 1.2, radius, -M_PI / 2, 0);
  cairo_arc(cr, x + w - radius, y + h - radius + 1.2, radius, 0, M_PI / 2);
  cairo_arc(cr, x + radius, y + h - radius + 1.2, radius, M_PI / 2, M_PI);
  cairo_arc(cr, x + radius, y + radius + 1.2, radius, M_PI, 3 * M_PI / 2);
  cairo_close_path(cr);
  cairo_set_source_rgba(cr, 0, 0, 0, 0.18);
  cairo_fill(cr);
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - radius, y + radius, radius, -M_PI / 2, 0);
  cairo_arc(cr, x + w - radius, y + h - radius, radius, 0, M_PI / 2);
  cairo_arc(cr, x + radius, y + h - radius, radius, M_PI / 2, M_PI);
  cairo_arc(cr, x + radius, y + radius, radius, M_PI, 3 * M_PI / 2);
  cairo_close_path(cr);
  // gradient fill for depth
  cairo_pattern_t *pat = cairo_pattern_create_linear(x, y, x, y + h);
  cairo_pattern_add_color_stop_rgb(pat, 0, r*1.12 >1?1:r*1.12, g*1.12>1?1:g*1.12, b*1.12>1?1:b*1.12);
  cairo_pattern_add_color_stop_rgb(pat, 1, r*0.88, g*0.88, b*0.88);
  cairo_set_source(cr, pat);
  cairo_fill_preserve(cr);
  cairo_pattern_destroy(pat);
  // subtle inner highlight top
  cairo_set_source_rgba(cr, 1, 1, 1, 0.18);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke_preserve(cr);
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - radius + 0.6, y + radius + 0.6, radius-1, -M_PI/2, 0);
  cairo_set_source_rgba(cr, 1,1,1,0.22);
  cairo_set_line_width(cr, 1.4);
  cairo_stroke(cr);
  cairo_destroy(cr);
  cairo_surface_destroy(surf);
  wlr_buffer_init(&buf->base, &icon_buf_impl, size, size);
  return &buf->base;
}

// Icon-Pfad suchen: erst direkt, dann in XDG Icon-Theme
char *find_icon(const char *name) {
  // Direkter Pfad (aengt mit / an)
  if (name && name[0] == '/') {
    FILE *f = fopen(name, "r");
    if (f) { fclose(f); return strdup(name); }
    return NULL;
  }

  // In Icon-Themes suchen
  const char *themes[] = { "hicolor", "Papirus", "Adwaita", "gnome", NULL };
  const char *bases[4];
  bases[0] = "/usr/share/icons";
  bases[1] = "/usr/local/share/icons";
  char local_icons[512];
  const char *home = getenv("HOME");
  if (home && home[0]) {
    snprintf(local_icons, sizeof(local_icons), "%s/.local/share/icons", home);
    bases[2] = local_icons;
    bases[3] = NULL;
  } else {
    bases[2] = NULL;
  }
  const char *sizes[] = { "scalable", "128x128", "96x96", "64x64", "48x48", "32x32", NULL };
  const char *exts[] = { ".png", ".svg", NULL };

  for (int b = 0; bases[b]; b++) {
    for (int t = 0; themes[t]; t++) {
      for (int s = 0; sizes[s]; s++) {
        for (int e = 0; exts[e]; e++) {
          // apps/ Verzeichnis probieren
          char path[512];
          snprintf(path, sizeof(path), "%s/%s/%s/apps/%s%s",
              bases[b], themes[t], sizes[s], name, exts[e]);
          FILE *f = fopen(path, "r");
          if (f) { fclose(f); return strdup(path); }

          // categories/ Verzeichnis probieren
          snprintf(path, sizeof(path), "%s/%s/%s/categories/%s%s",
              bases[b], themes[t], sizes[s], name, exts[e]);
          f = fopen(path, "r");
          if (f) { fclose(f); return strdup(path); }
        }
      }
    }
  }
  return NULL;
}

// Config-Datei einlesen (neues Format mit | getrennten Feldern)
// Format: name=command=r,g,b|icon=/path.png|home=true
static void load_apps_from_file(struct phone_launcher *launcher) {
  const char *config_home = getenv("XDG_CONFIG_HOME");
  char path[512];

  if (config_home && config_home[0])
    snprintf(path, sizeof(path), "%s/phone-wm/launcher.conf", config_home);
  else {
    const char *home = getenv("HOME");
    if (home && home[0])
      snprintf(path, sizeof(path), "%s/.config/phone-wm/launcher.conf", home);
    else return;
  }

  FILE *f = fopen(path, "r");
  if (!f) {
    snprintf(path, sizeof(path), "/etc/phone-wm/launcher.conf");
    f = fopen(path, "r");
  }
  if (!f) return;

  int count = 0;
  char line[512];
  while (count < MAX_APPS && fgets(line, sizeof(line), f)) {
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
      line[--len] = '\0';

    if (len == 0 || line[0] == '#') continue;

    struct phone_app *app = &launcher->apps[count];
    memset(app, 0, sizeof(*app));
    app->color[0] = 0.5f; app->color[1] = 0.5f; app->color[2] = 0.5f; app->color[3] = 1.0f;

    // Base-Zeile: name=command=r,g,b
    char *base = strtok(line, "|");
    if (!base) continue;

    char *eq1 = strchr(base, '=');
    if (!eq1) continue;
    *eq1 = '\0';
    app->name = strdup(base);

    char *cmd = eq1 + 1;
    char *eq2 = strchr(cmd, '=');
    if (!eq2) { free(app->name); app->name = NULL; continue; }
    *eq2 = '\0';
    app->command = strdup(cmd);

    char *colors = eq2 + 1;
    sscanf(colors, "%f,%f,%f", &app->color[0], &app->color[1], &app->color[2]);
    app->color[3] = 1.0f;

    // Optionale Pipe-Felder parsen: icon=path, home=true
    char *rest = NULL;
    while ((rest = strtok(NULL, "|")) != NULL) {
      if (strncmp(rest, "icon=", 5) == 0)
        app->icon_path = strdup(rest + 5);
      else if (strcmp(rest, "home") == 0 || strcmp(rest, "home=true") == 0)
        app->home = true;
    }

    count++;
  }

  fclose(f);
  if (count > 0) {
    launcher->num_apps = count;
    wlr_log(WLR_INFO, "Loaded %d apps from config", count);
  }
}

// launcher.conf aus den aktuellen Apps schreiben (format: name=command=r,g,b|icon=...|home)
void launcher_write_config(struct phone_launcher *launcher) {
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
  snprintf(path, sizeof(path), "%s/" "launcher.conf", dir);

  FILE *f = fopen(path, "w");
  if (!f) return;

  for (int i = 0; i < launcher->num_apps; i++) {
    struct phone_app *app = &launcher->apps[i];
    if (!app->name) continue;
    fprintf(f, "%s=%s=%.2f,%.2f,%.2f",
        app->name, app->command ? app->command : "",
        app->color[0], app->color[1], app->color[2]);
    if (app->icon_path) fprintf(f, "|icon=%s", app->icon_path);
    if (app->home) fprintf(f, "|home");
    fprintf(f, "\n");
  }
  fclose(f);
  wlr_log(WLR_INFO, "Wrote %d apps to %s", launcher->num_apps, path);
}

// Einzelnes Icon-Scene-Buffer-Objekt erstellen
static void create_icon_scene(struct phone_app *app, struct phone_app_icon *icon,
    struct wlr_scene_tree *parent, int size) {
  icon->app = app;
  icon->w = size;
  icon->h = size;

  // Versuche PNG-Icon zu laden, sonst Fallback-Farbfeld
  char *found = find_icon(app->icon_path ? app->icon_path : app->name);
  if (found) {
    icon->icon_wlr_buf = load_icon_png(found, size);
    free(found);
  }
  if (!icon->icon_wlr_buf)
    icon->icon_wlr_buf = create_color_icon(app->color[0], app->color[1], app->color[2], size);

  if (icon->icon_wlr_buf) {
    icon->icon_buf = wlr_scene_buffer_create(parent, icon->icon_wlr_buf);
    wlr_scene_buffer_set_source_box(icon->icon_buf,
        &(struct wlr_fbox){0, 0, size, size});
    wlr_buffer_drop(icon->icon_wlr_buf);
    icon->icon_wlr_buf = NULL;
  }
}

// =============================================
// Launcher (App-Schublade)
// =============================================

// Kleiner Hash für stabile Icon-Farben aus dem App-Namen
static unsigned desktop_hash(const char *s) {
  unsigned h = 5381;
  while (*s) { h = h * 33 + (unsigned char)*s; s++; }
  return h;
}

// Aus "Exec=" das ausführbare Kommando extrahieren (erstes Token, Quotes raus)
static void exec_command(const char *exec, char *out, size_t out_len) {
  char tmp[256];
  snprintf(tmp, sizeof(tmp), "%s", exec ? exec : "");
  char *space = strchr(tmp, ' ');
  if (space) *space = '\0';
  char *start = tmp;
  if (*start == '"' || *start == '\'') {
    start++;
    size_t l = strlen(start);
    if (l && (start[l - 1] == '"' || start[l - 1] == '\'')) start[l - 1] = '\0';
  }
  snprintf(out, out_len, "%s", start);
}

// Einen Ordner nach .desktop-Dateien durchsuchen und freie Slots füllen
static void launcher_scan_desktop(struct phone_launcher *l, const char *dir) {
  char pattern[512];
  snprintf(pattern, sizeof(pattern), "%s/*.desktop", dir);

  glob_t g;
  if (glob(pattern, 0, NULL, &g) != 0) return;

  for (size_t i = 0; i < g.gl_pathc && l->num_apps < MAX_APPS; i++) {
    // Steam-Apps überspringen
    if (strcasestr(g.gl_pathv[i], "steam")) continue;

    FILE *f = fopen(g.gl_pathv[i], "r");
    if (!f) continue;

    char name[256] = "", exec[256] = "", icon[256] = "";
    bool skip = false, in_entry = false, is_app = false;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
      size_t len = strlen(line);
      while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
        line[--len] = '\0';
      if (len == 0) continue;
      if (line[0] == '[') {
        in_entry = strcmp(line, "[Desktop Entry]") == 0;
        continue;
      }
      if (!in_entry) continue;
      if (strncmp(line, "Type=", 5) == 0)
        is_app = strcmp(line + 5, "Application") == 0;
      else if (strncmp(line, "NoDisplay=", 10) == 0)
        skip = skip || strcmp(line + 10, "true") == 0;
      else if (strncmp(line, "Hidden=", 7) == 0)
        skip = skip || strcmp(line + 7, "true") == 0;
      else if (strncmp(line, "Terminal=", 9) == 0)
        skip = skip || strcmp(line + 9, "true") == 0;
      else if (strncmp(line, "Categories=", 11) == 0) {
        // Spiele filtern
        if (strcasestr(line + 11, "Game"))
          skip = true;
      }
      else if (strncmp(line, "Name=", 5) == 0 && !name[0])
        snprintf(name, sizeof(name), "%s", line + 5);
      else if (strncmp(line, "Exec=", 5) == 0 && !exec[0])
        snprintf(exec, sizeof(exec), "%s", line + 5);
      else if (strncmp(line, "Icon=", 5) == 0 && !icon[0])
        snprintf(icon, sizeof(icon), "%s", line + 5);
    }
    fclose(f);

    if (skip || !is_app || !name[0] || !exec[0]) continue;

    // Duplikate vermeiden (gleicher Name schon vorhanden)
    bool dup = false;
    for (int k = 0; k < l->num_apps; k++) {
      if (strcmp(l->apps[k].name, name) == 0) dup = true;
    }
    if (dup) continue;

    struct phone_app *app = &l->apps[l->num_apps];
    memset(app, 0, sizeof(*app));
    app->name = strdup(name);
    char cmd[128];
    exec_command(exec, cmd, sizeof(cmd));
    app->command = strdup(cmd);
    if (icon[0]) app->icon_path = strdup(icon);

    unsigned h = desktop_hash(name);
    app->color[0] = 0.25f + ((h >> 16) & 0xFF) / 480.0f;
    app->color[1] = 0.25f + ((h >> 8) & 0xFF) / 480.0f;
    app->color[2] = 0.25f + (h & 0xFF) / 480.0f;
    app->color[3] = 1.0f;
    l->num_apps++;
  }

  globfree(&g);
}

// Launcher initialisieren: Apps laden, Icons erzeugen, Scene-Tree anlegen
void launcher_init(struct phone_server *server) {
  struct phone_launcher *launcher = calloc(1, sizeof(struct phone_launcher));
  if (!launcher) return;
  server->launcher = launcher;
  launcher->server = server;
  launcher->visible = false;

  // Defaults laden
  int num_defaults = sizeof(default_apps) / sizeof(default_apps[0]);
  launcher->num_apps = num_defaults < MAX_APPS ? num_defaults : MAX_APPS;
  memcpy(launcher->apps, default_apps, launcher->num_apps * sizeof(struct phone_app));

  // Config drueber laden
  load_apps_from_file(launcher);

  // Freie Plätze mit .desktop-Apps aus dem System füllen
  if (launcher->num_apps < MAX_APPS) {
    const char *home = getenv("HOME");
    if (home && home[0]) {
      char dir[512];
      snprintf(dir, sizeof(dir), "%s/.local/share/applications", home);
      launcher_scan_desktop(launcher, dir);
    }
    launcher_scan_desktop(launcher, "/usr/local/share/applications");
    launcher_scan_desktop(launcher, "/usr/share/applications");
  }

  // Scene-Tree im Overlay-Layer
  launcher->tree = wlr_scene_tree_create(server->layer_overlay_tree);
  launcher->tree->node.data = launcher;

  // Responsive sizing based on logical output size
  int screen_w, screen_h; float scale;
  anim_get_logical_size(server, &screen_w, &screen_h, &scale);
  int gap = anim_scaled(LAUNCHER_ICON_GAP, scale);
  int pad = anim_scaled(LAUNCHER_PADDING, scale);
  int cols = LAUNCHER_COLS;
  int top = anim_scaled(LAUNCHER_TOP_OFFSET, scale) + gap;
  // compute icon size to fit width responsively
  int w = (screen_w - 2*pad - (cols-1)*gap) / cols;
  if (w > anim_scaled(LAUNCHER_ICON_WIDTH, scale)) w = anim_scaled(LAUNCHER_ICON_WIDTH, scale);
  if (w < 56) w = 56;
  int h = w;
  int grid_w = cols * w + (cols-1)*gap;
  int start_x = pad + (screen_w - 2*pad - grid_w)/2;

  // iOS-like blurred translucent backdrop — wallpaper shows through
  float panel_r = 0.08f, panel_g = 0.08f, panel_b = 0.10f;
  if (server->theme) { panel_r = server->theme->panel_bg[0]; panel_g = server->theme->panel_bg[1]; panel_b = server->theme->panel_bg[2]; }
  launcher->bg_rect = wlr_scene_rect_create(
      launcher->tree, screen_w, 100, (float[4]){panel_r, panel_g, panel_b, 0.97f});
  wlr_scene_node_set_position(&launcher->bg_rect->node, 0, 0);

  // Pillen-Suche — wie Spotlight (magnifying glass, centred)
  launcher->filter[0] = '\0';
  launcher->search_w = screen_w - 2 * pad - 40;
  if (launcher->search_w > 420) launcher->search_w = 420;
  launcher->search_h = 36;
  launcher->search_x = (screen_w - launcher->search_w)/2;
  launcher->search_y = 14;
  launcher->search_bg = wlr_scene_rect_create(launcher->tree,
      launcher->search_w, launcher->search_h,
      (float[4]){0.18f, 0.18f, 0.20f, 0.96f});
  wlr_scene_node_set_position(&launcher->search_bg->node,
      launcher->search_x, launcher->search_y);

  // Icons + Labels (echtes Phone: Icon + Name darunter, squircle)
  for (int i = 0; i < launcher->num_apps; i++) {
    struct phone_app_icon *icon = &launcher->icons[i];
    int col = i % cols;
    int row = i / cols;
    icon->x = start_x + col * (w + gap);
    icon->y = top + row * (h + gap + 14); // extra room for label
    icon->base_x = icon->x;
    icon->base_y = icon->y;

    create_icon_scene(&launcher->apps[i], icon, launcher->tree, w);
    if (icon->icon_buf)
      wlr_scene_node_set_position(&icon->icon_buf->node, icon->x, icon->y);
    // Label under icon
    const char *n = launcher->apps[i].name ? launcher->apps[i].name : "";
    struct wlr_buffer *lbuf = phone_render_text(server->allocator, n, w + 10, 14, 11, 0.97f, 0.97f, 0.98f);
    if (lbuf) {
      icon->label_buf = wlr_scene_buffer_create(launcher->tree, lbuf);
      icon->label_x = icon->x - 5; icon->label_y = icon->y + w + 3; icon->label_w = w+10; icon->label_h = 14;
      wlr_scene_node_set_position(&icon->label_buf->node, icon->label_x, icon->label_y);
      wlr_buffer_drop(lbuf);
    }
  }

  int grid_h = ((launcher->num_apps + cols - 1) / cols) * (h + gap);
  int content_h = top + grid_h + gap;
  if (content_h < 100) content_h = 100;
  // Hintergrund um das Suchfeld erhöhen
  wlr_scene_rect_set_size(launcher->bg_rect, screen_w, content_h + 20);

  wlr_scene_node_set_enabled(&launcher->tree->node, false);
}

void launcher_show(struct phone_launcher *launcher) {
  if (!launcher || launcher->visible) return;
  if (launcher->server->control_center && launcher->server->control_center->visible) control_center_hide(launcher->server->control_center);
  launcher->filter[0]='\0';
  launcher_search_apply(launcher);
  int sw, sh; float sc; anim_get_logical_size(launcher->server, &sw, &sh, &sc);
  // rebuild geometry for current size (icon size may have changed with rotation)
  // we keep current tree enabled and slide from bottom
  launcher->visible = true;
  wlr_scene_node_set_enabled(&launcher->tree->node, true);
  anim_cancel(launcher->tree);
  anim_slide_y(launcher->server, launcher->tree, sh, 0, ANIM_DURATION_MS, NULL, NULL);
}


static void launcher_hide_done(void *data) {
  struct phone_launcher *l = data;
  if (l && !l->visible) wlr_scene_node_set_enabled(&l->tree->node, false);
}

void launcher_hide(struct phone_launcher *launcher) {
  if (!launcher || !launcher->visible) return;
  int sw, sh; float sc; anim_get_logical_size(launcher->server, &sw, &sh, &sc);
  launcher->visible = false;
  anim_slide_y(launcher->server, launcher->tree, 0, sh, ANIM_DURATION_SHORT, launcher_hide_done, launcher);
}

// Launcher ein-/ausblenden (Umschalter)
void launcher_toggle(struct phone_launcher *launcher) {
  if (launcher->visible) launcher_hide(launcher);
  else launcher_show(launcher);
}

// Tap im Launcher: App-Icon finden und starten
void launcher_handle_tap(struct phone_launcher *launcher, double x, double y) {
  if (!launcher->visible) return;

  // Tap auf das Suchfeld: nur die Tastatur-Fokus-Sperre prüfen (keine Aktion)
  if (x >= launcher->search_x && x < launcher->search_x + launcher->search_w &&
      y >= launcher->search_y && y < launcher->search_y + launcher->search_h) {
    return;
  }

  for (int i = 0; i < launcher->num_apps; i++) {
    struct phone_app_icon *icon = &launcher->icons[i];
    if (x >= icon->x && x < icon->x + icon->w &&
        y >= icon->y && y < icon->y + icon->h) {
      wlr_log(WLR_INFO, "launching: %s", icon->app->command);
      pid_t pid = fork();
      if (pid == 0) {
        setsid();
        execlp("/bin/sh", "sh", "-c", icon->app->command, NULL);
        _exit(1);
      }
      launcher_hide(launcher);
      return;
    }
  }
  launcher_hide(launcher);
}

// =============================================
// App-Suche: Tastendrücke auswerten und App-Grid filtern
// =============================================

// Suchtext-Feld — Spotlight pill with icon
static void launcher_render_search(struct phone_launcher *l) {
  struct phone_server *server = l->server;
  if (l->search_buf) {
    wlr_scene_node_destroy(&l->search_buf->node);
    l->search_buf = NULL;
  }
  char label[80];
  if (l->filter[0])
    snprintf(label, sizeof(label), "\u2315  %s|", l->filter);
  else
    snprintf(label, sizeof(label), "\u2315  App suchen...");

  struct wlr_buffer *buf = phone_render_text(server->allocator,
      label, l->search_w, l->search_h, 13, 0.96f, 0.96f, 0.98f);
  if (buf) {
    l->search_buf = wlr_scene_buffer_create(l->tree, buf);
    wlr_scene_node_set_position(&l->search_buf->node, l->search_x, l->search_y);
    wlr_buffer_drop(buf);
  }
}

// Suchtext anwenden: passende Apps einblenden und kompakt anordnen
void launcher_search_apply(struct phone_launcher *l) {
  int screen_w = 800;
  struct phone_output *output = NULL;
  if (!wl_list_empty(&l->server->outputs)) {
    output = wl_container_of(l->server->outputs.next, output, link);
    screen_w = output->wlr_output->width;
  }

  // Übereinstimmende Apps sammeln (Groß-/Kleinschreibung egal)
  int matched[MAX_APPS];
  int n = 0;
  if (!l->filter[0]) {
    for (int i = 0; i < l->num_apps; i++)
      matched[n++] = i;
  } else {
    for (int i = 0; i < l->num_apps; i++) {
      if (strcasestr(l->apps[i].name, l->filter))
        matched[n++] = i;
    }
  }

  int screen_w2, screen_h2; float sc2; anim_get_logical_size(l->server, &screen_w2, &screen_h2, &sc2);
  int gap = anim_scaled(LAUNCHER_ICON_GAP, sc2);
  int pad = anim_scaled(LAUNCHER_PADDING, sc2);
  int cols = LAUNCHER_COLS;
  int h = anim_scaled(LAUNCHER_ICON_HEIGHT, sc2); // placeholder, will be recomputed as w
  int w = (screen_w2 - 2*pad - (cols-1)*gap) / cols;
  if (w > anim_scaled(LAUNCHER_ICON_WIDTH, sc2)) w = anim_scaled(LAUNCHER_ICON_WIDTH, sc2);
  if (w < 56) w = 56;
  h = w;
  int grid_w = cols * w + (cols-1)*gap;
  int start_x = pad + (screen_w2 - 2*pad - grid_w)/2;
  int top = anim_scaled(LAUNCHER_TOP_OFFSET, sc2) + gap;

  // Alle Icons + Labels zunächst ausblenden
  for (int i = 0; i < l->num_apps; i++) {
    if (l->icons[i].icon_buf)
      wlr_scene_node_set_enabled(&l->icons[i].icon_buf->node, false);
    if (l->icons[i].label_buf)
      wlr_scene_node_set_enabled(&l->icons[i].label_buf->node, false);
  }

  // Treffer anzeigen und von oben kompakt anordnen
  for (int k = 0; k < n; k++) {
    struct phone_app_icon *icon = &l->icons[matched[k]];
    if (!icon->icon_buf) continue;
    if (!l->filter[0]) {
      icon->x = icon->base_x;
      icon->y = icon->base_y;
    } else {
      int col = k % cols;
      int row = k / cols;
      icon->x = start_x + col * (w + gap);
      icon->y = top + row * (h + gap + 14);
    }
    wlr_scene_node_set_enabled(&icon->icon_buf->node, true);
    wlr_scene_node_set_position(&icon->icon_buf->node, icon->x, icon->y);
    if (icon->label_buf) {
      wlr_scene_node_set_enabled(&icon->label_buf->node, true);
      wlr_scene_node_set_position(&icon->label_buf->node, icon->x -5, icon->y + w + 3);
    }
  }

  // Suchtext neu rendern
  launcher_render_search(l);
}

// Tastendruck für die App-Suche verarbeiten (evdev-Scancodes)
void launcher_search_key(struct phone_launcher *l, uint32_t keycode) {
  if (!l || !l->visible) return;

  char ch = 0;
  if (keycode >= KEY_A && keycode <= KEY_Z) {
    ch = 'a' + (keycode - KEY_A);
  } else if (keycode >= KEY_1 && keycode <= KEY_0) {
    // Achtung: KEY_1..KEY_0 sind nicht ganz aufsteigend, deshalb einzeln
    static const char digits[] = "1234567890";
    ch = digits[keycode - KEY_1];
  } else if (keycode == KEY_BACKSPACE) {
    size_t len = strlen(l->filter);
    if (len > 0) l->filter[len - 1] = '\0';
    launcher_search_apply(l);
    return;
  } else if (keycode == KEY_SPACE) {
    ch = ' ';
  } else if (keycode == KEY_DOT) {
    ch = '.';
  } else if (keycode == KEY_MINUS) {
    ch = '-';
  } else if (keycode == KEY_ENTER || keycode == KEY_KPENTER) {
    // Enter: erste passende App starten
    for (int i = 0; i < l->num_apps; i++) {
      if (!l->filter[0] || strcasestr(l->apps[i].name, l->filter)) {
        wlr_log(WLR_INFO, "search launch: %s", l->apps[i].command);
        pid_t pid = fork();
        if (pid == 0) {
          setsid();
          execlp("/bin/sh", "sh", "-c", l->apps[i].command, NULL);
          _exit(1);
        }
        break;
      }
    }
    launcher_hide(l);
    return;
  } else if (keycode == KEY_ESC) {
    // Esc: Suche leeren, beim zweiten Mal Schublade schließen
    if (l->filter[0]) {
      l->filter[0] = '\0';
      launcher_search_apply(l);
    } else {
      launcher_hide(l);
    }
    return;
  }

  if (ch) {
    size_t len = strlen(l->filter);
    if (len + 1 < sizeof(l->filter)) {
      l->filter[len] = ch;
      l->filter[len + 1] = '\0';
    }
    launcher_search_apply(l);
  }
}

// =============================================
// Home Screen (immer sichtbare Icons auf dem Desktop)
// =============================================

// Home-Screen initialisieren (Icons im Bottom-Layer anlegen)
void homescreen_init(struct phone_server *server) {
  struct phone_homescreen *hs = calloc(1, sizeof(struct phone_homescreen));
  if (!hs) return;
  server->homescreen = hs;
  hs->server = server;

  // Im Bottom-Layer, damit es auf ALLEN Workspaces hinter Apps sichtbar ist
  hs->tree = wlr_scene_tree_create(server->layer_bottom_tree);
  hs->tree->node.data = hs;

  homescreen_update(hs);
}

// Home-Screen-Icons neu aufbauen (Apps mit home=true anzeigen)
void homescreen_update(struct phone_homescreen *hs) {
  if (!hs) return;
  struct phone_server *server = hs->server;

  // Alte Icons wegmachen
  for (int i = 0; i < hs->num_icons; i++) {
    struct phone_app_icon *icon = &hs->icons[i];
    if (icon->icon_buf) {
      wlr_scene_node_destroy(&icon->icon_buf->node);
      icon->icon_buf = NULL;
    }
  }
  hs->num_icons = 0;

  // Apps mit home=true sammeln
  struct phone_launcher *launcher = server->launcher;
  if (!launcher) return;

  int home_apps[MAX_APPS];
  int num_home = 0;
  for (int i = 0; i < launcher->num_apps && num_home < MAX_APPS; i++) {
    if (launcher->apps[i].home)
      home_apps[num_home++] = i;
  }
  if (num_home == 0) return;

  // Bildschirm-Dimensionen holen
  int screen_w = 800, screen_h = 480;
  struct phone_output *output = NULL;
  if (!wl_list_empty(&server->outputs)) {
    output = wl_container_of(server->outputs.next, output, link);
    screen_w = output->wlr_output->width;
    screen_h = output->wlr_output->height;
  }

  int bar_h = server->bar ? server->bar->height : 0;
  int lw2,lh2; float sc2; anim_get_logical_size(server, &lw2,&lh2,&sc2);
  int icon_size = anim_scaled(HOME_ICON_SIZE, sc2);
  int gap = anim_scaled(HOME_ICON_GAP, sc2);
  int cols = HOME_COLS;
  int rows = (num_home + cols - 1) / cols;

  int grid_w = cols * icon_size + (cols - 1) * gap;
  int grid_h = rows * icon_size + (rows - 1) * gap;
  int start_x = (screen_w - grid_w) / 2;
  int start_y = screen_h - bar_h - HOME_BOTTOM_OFFSET - grid_h;

  for (int i = 0; i < num_home; i++) {
    int app_idx = home_apps[i];
    struct phone_app_icon *icon = &hs->icons[i];
    int col = i % cols;
    int row = i / cols;
    icon->x = start_x + col * (icon_size + gap);
    icon->y = start_y + row * (icon_size + gap + 12);

    create_icon_scene(&launcher->apps[app_idx], icon, hs->tree, icon_size);
    if (icon->icon_buf)
      wlr_scene_node_set_position(&icon->icon_buf->node, icon->x, icon->y);
    const char *n = launcher->apps[app_idx].name ? launcher->apps[app_idx].name : "";
    struct wlr_buffer *lbuf = phone_render_text(server->allocator, n, icon_size+10, 13, 11, 0.97f,0.97f,0.98f);
    if (lbuf) {
      icon->label_buf = wlr_scene_buffer_create(hs->tree, lbuf);
      wlr_scene_node_set_position(&icon->label_buf->node, icon->x -5, icon->y + icon_size + 2);
      wlr_buffer_drop(lbuf);
    }
  }
  hs->num_icons = num_home;
}

// Tap auf dem Home-Screen: App-Icon finden und starten
void homescreen_handle_tap(struct phone_homescreen *hs, double x, double y) {
  if (!hs || hs->num_icons == 0) return;

  for (int i = 0; i < hs->num_icons; i++) {
    struct phone_app_icon *icon = &hs->icons[i];
    if (x >= icon->x && x < icon->x + icon->w &&
        y >= icon->y && y < icon->y + icon->h) {
      wlr_log(WLR_INFO, "homescreen launch: %s", icon->app->command);
      pid_t pid = fork();
      if (pid == 0) {
        setsid();
        execlp("/bin/sh", "sh", "-c", icon->app->command, NULL);
        _exit(1);
      }
      return;
    }
  }
}
