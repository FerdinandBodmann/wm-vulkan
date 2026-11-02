#define _POSIX_C_SOURCE 200809L
#include "server.h"
#include "controlcenter.h"
#include "hw.h"
#include "lock.h"
#include "notify.h"
#include "osd.h"
#include "power.h"
#include "render.h"
#include "settings.h"
#include "theme.h"
#include "anim.h"
#include "toast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wlr/render/allocator.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#define CC_PADDING 16
#define CC_GAP 12

// Kleine Helfer: Datei einlesen (für Akku/sysfs)
static int cc_read_file_str(const char *path, char *buf, size_t len) {
  FILE *f = fopen(path, "r");
  if (!f) return -1;
  size_t n = fread(buf, 1, len - 1, f);
  fclose(f);
  if (n == 0) return -1;
  buf[n] = '\0';
  while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
    buf[--n] = '\0';
  return (int)n;
}

// Akkustand aus sysfs lesen (z.B. "87%")
static void cc_read_battery(char *buf, size_t len) {
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
    if (cc_read_file_str(path, cap, sizeof(cap)) >= 0) break;
  }
  if (cap[0] == '?') {
    snprintf(buf, len, "AC");
  } else {
    snprintf(buf, len, "\u25A0 %s%%", cap);
  }
}

// Aktuelle Uhrzeit (HH:MM) in den Puffer schreiben
static void cc_read_time(char *buf, size_t len) {
  time_t now = time(NULL);
  struct tm *tm = localtime(&now);
  strftime(buf, len, "%H:%M", tm);
}

// Beschriftung — mit kleinem Glyph wie iOS (über Text)
static const char *cc_tile_label(int id) {
  switch (id) {
  case CC_TILE_WIFI:       return "\u25CE WLAN";
  case CC_TILE_BLUETOOTH:  return "\u25C8 BT";
  case CC_TILE_AIRPLANE:   return "\u2708 Flug";
  case CC_TILE_FLASHLIGHT: return "\u2600 Licht";
  case CC_TILE_NIGHTLIGHT: return "\u263E Nacht";
  case CC_TILE_SCREENSHOT: return "\u25A3 Screen";
  case CC_TILE_DND:        return "\u263E Ruhe";
  case CC_TILE_SILENT:     return "\u266A Stumm";
  }
  return "";
}

// Farbe einer Kachel — aktive getönt, inaktive glasig transluzent
static void cc_tile_color(struct phone_server *server, int id, bool active, float out[4]) {
  struct phone_theme *theme = server->theme;
  if (!active) {
    if (theme) { out[0]=theme->tile_inactive[0]; out[1]=theme->tile_inactive[1]; out[2]=theme->tile_inactive[2]; out[3]=1.0f; }
    else { out[0]=0.24f; out[1]=0.24f; out[2]=0.28f; out[3]=1.0f; }
    return;
  }
  switch (id) {
  case CC_TILE_WIFI:       out[0] = 0.15f; out[1] = 0.45f; out[2] = 0.95f; break;
  case CC_TILE_BLUETOOTH:  out[0] = 0.10f; out[1] = 0.55f; out[2] = 0.90f; break;
  case CC_TILE_AIRPLANE:   out[0] = 0.95f; out[1] = 0.60f; out[2] = 0.10f; break;
  case CC_TILE_FLASHLIGHT: out[0] = 0.95f; out[1] = 0.90f; out[2] = 0.50f; break;
  case CC_TILE_NIGHTLIGHT: out[0] = 0.62f; out[1] = 0.35f; out[2] = 0.95f; break;
  case CC_TILE_SCREENSHOT: out[0] = 0.30f; out[1] = 0.75f; out[2] = 0.45f; break;
  case CC_TILE_DND:        out[0] = 0.60f; out[1] = 0.35f; out[2] = 1.00f; break;
  case CC_TILE_SILENT:     out[0] = 0.45f; out[1] = 0.50f; out[2] = 0.68f; break;
  }
  out[3] = 1.0f;
}

// Kachel-Neufärben nach Zustandswechsel (abgerundeten Hintergrund neu rendern)
static void cc_recolor_tile(struct phone_control_center *cc, int id) {
  struct phone_cc_tile *t = &cc->tiles[id];
  if (!t->bg) return;
  float color[4];
  cc_tile_color(cc->server, id, t->active, color);
  wlr_scene_node_destroy(&t->bg->node);
  struct wlr_buffer *buf = phone_render_glass(cc->server->allocator,
      t->w, t->h, 18, color[0], color[1], color[2], color[3]);
  if (buf) {
    t->bg = wlr_scene_buffer_create(cc->tree, buf);
    wlr_scene_node_set_position(&t->bg->node, t->x, t->y);
    wlr_buffer_drop(buf);
  } else {
    t->bg = NULL;
  }
}

// Slider-Füllung an den aktuellen Wert anpassen
static void cc_update_slider(struct phone_cc_slider *s) {
  if (!s->fill) return;
  int fill_w = (int)(s->w * s->value);
  if (fill_w < 1) fill_w = 1;
  if (fill_w > s->w) fill_w = s->w;
  wlr_scene_rect_set_size(s->fill, fill_w, s->h);
}

// Alle Scene-Nodes zerstören (Zustand in den Structs bleibt erhalten)
static void cc_clear(struct phone_control_center *cc) {
  if (cc->bg_rect) {
    wlr_scene_node_destroy(&cc->bg_rect->node);
    cc->bg_rect = NULL;
  }
  if (cc->panel_buf) {
    wlr_scene_node_destroy(&cc->panel_buf->node);
    cc->panel_buf = NULL;
  }
  if (cc->info_buf) {
    wlr_scene_node_destroy(&cc->info_buf->node);
    cc->info_buf = NULL;
  }

  for (int i = 0; i < CC_NUM_TILES; i++) {
    struct phone_cc_tile *t = &cc->tiles[i];
    if (t->bg) {
      wlr_scene_node_destroy(&t->bg->node);
      t->bg = NULL;
    }
    if (t->label) {
      wlr_scene_node_destroy(&t->label->node);
      t->label = NULL;
    }
  }

  for (int i = 0; i < CC_POWER_BTNS; i++) {
    struct phone_cc_tile *t = &cc->power_btns[i];
    if (t->bg) {
      wlr_scene_node_destroy(&t->bg->node);
      t->bg = NULL;
    }
    if (t->label) {
      wlr_scene_node_destroy(&t->label->node);
      t->label = NULL;
    }
  }

  struct phone_cc_slider *sliders[] = { &cc->brightness, &cc->volume };
  for (size_t i = 0; i < sizeof(sliders) / sizeof(sliders[0]); i++) {
    struct phone_cc_slider *s = sliders[i];
    if (s->track) {
      wlr_scene_node_destroy(&s->track->node);
      s->track = NULL;
    }
    if (s->fill) {
      wlr_scene_node_destroy(&s->fill->node);
      s->fill = NULL;
    }
    if (s->label) {
      wlr_scene_node_destroy(&s->label->node);
      s->label = NULL;
    }
  }
}

// Slider — Kapsel (wie iOS): 28px hohe Pille, innen Fill + Thumb-Knopf
static void cc_build_slider(struct phone_control_center *cc,
    struct phone_cc_slider *s, const char *text, int sw, int y) {
  struct phone_theme *theme = cc->server->theme;
  s->x = CC_PADDING;
  s->y = y;
  s->w = sw - 2 * CC_PADDING;
  s->h = 28; // capsule

  // track is the pill background
  s->track = wlr_scene_rect_create(cc->tree, s->w, s->h,
      (float[4]){ 0.22f, 0.22f, 0.26f, 0.88f });
  wlr_scene_node_set_position(&s->track->node, s->x, s->y);

  int fill_w = (int)(s->w * s->value);
  if (fill_w < 14) fill_w = 14;
  if (fill_w > s->w) fill_w = s->w;
  float acc_r = theme ? theme->accent[0] : 0.38f;
  float acc_g = theme ? theme->accent[1] : 0.62f;
  float acc_b = theme ? theme->accent[2] : 1.0f;
  s->fill = wlr_scene_rect_create(cc->tree, fill_w, s->h,
      (float[4]){ acc_r, acc_g, acc_b, 0.96f });
  wlr_scene_node_set_position(&s->fill->node, s->x, s->y);

  struct wlr_buffer *buf = phone_render_text(cc->server->allocator,
      text, 130, 18, 11,
      theme ? theme->fg[0] : 0.96f,
      theme ? theme->fg[1] : 0.96f,
      theme ? theme->fg[2] : 0.96f);
  if (buf) {
    s->label = wlr_scene_buffer_create(cc->tree, buf);
    wlr_scene_node_set_position(&s->label->node, s->x + 8, y - 20);
    wlr_buffer_drop(buf);
  }
}

// Control Center initialisieren (Scene-Tree im Overlay anlegen)
void control_center_init(struct phone_server *server) {
  struct phone_control_center *cc = calloc(1, sizeof(struct phone_control_center));
  if (!cc) return;
  server->control_center = cc;
  cc->server = server;
  cc->visible = false;
  cc->brightness.value = 0.7f;
  cc->volume.value = 0.5f;

  // Im Overlay-Layer, über Launcher & Recents (wird zuletzt erstellt)
  cc->tree = wlr_scene_tree_create(server->layer_overlay_tree);
  cc->tree->node.data = cc;

  wlr_scene_node_set_enabled(&cc->tree->node, false);
  wlr_log(WLR_INFO, "%s", "Control center initialized");
}

// Abgerundete Kachel mit Text erzeugen (Hintergrund + Label)
static void cc_build_tile(struct phone_control_center *cc, int id) {
  struct phone_cc_tile *t = &cc->tiles[id];
  float color[4];
  cc_tile_color(cc->server, id, t->active, color);
  struct wlr_buffer *buf = phone_render_glass(cc->server->allocator,
      t->w, t->h, 18, color[0], color[1], color[2], color[3]);
  if (buf) {
    t->bg = wlr_scene_buffer_create(cc->tree, buf);
    wlr_scene_node_set_position(&t->bg->node, t->x, t->y);
    wlr_buffer_drop(buf);
  }
  struct wlr_buffer *lbuf = phone_render_text(cc->server->allocator,
      cc_tile_label(id), t->w, t->h, 12, 1.0f, 1.0f, 1.0f);
  if (lbuf) {
    t->label = wlr_scene_buffer_create(cc->tree, lbuf);
    wlr_scene_node_set_position(&t->label->node, t->x, t->y);
    wlr_buffer_drop(lbuf);
  }
}

static void cc_show_done(void *data) { (void)data; }
static void cc_hide_done(void *data) {
  struct phone_control_center *cc = data;
  if (cc && !cc->visible) wlr_scene_node_set_enabled(&cc->tree->node, false);
}

// Control Center öffnen: Panel mit Kacheln und Slidern aufbauen
void control_center_show(struct phone_control_center *cc) {
  if (!cc || cc->visible) return;
  struct phone_server *server = cc->server;

  // Andere Overlays schließen
  if (server->launcher && server->launcher->visible)
    launcher_hide(server->launcher);
  if (server->recents && server->recents->visible)
    recents_hide(server->recents);
  if (server->settings && server->settings->visible)
    settings_hide(server->settings);
  if (server->toast && server->toast->visible && server->dnd_enabled)
    toast_hide(server->toast);

  int lw, lh; float lsc; anim_get_logical_size(server, &lw, &lh, &lsc);
  cc->screen_w = lw; cc->screen_h = lh;

  cc_clear(cc);

  int sw = cc->screen_w;
  int sh = cc->screen_h;

  // Echten Hardware-Zustand abfragen (falls Backend fehlt: -1 → aus)
  cc->tiles[CC_TILE_WIFI].active = hw_wifi_state() > 0;
  cc->tiles[CC_TILE_BLUETOOTH].active = hw_bluetooth_state() > 0;
  cc->tiles[CC_TILE_AIRPLANE].active = hw_airplane_state() > 0;
  cc->tiles[CC_TILE_FLASHLIGHT].active = hw_torch_state() > 0;
  cc->tiles[CC_TILE_NIGHTLIGHT].active = night_get(server);
  cc->tiles[CC_TILE_DND].active = server->dnd_enabled;
  cc->tiles[CC_TILE_SILENT].active = server->silent;
  int bl = hw_backlight_value();
  int vol = hw_volume_value();
  if (bl >= 0) cc->brightness.value = bl / 100.0f;
  if (vol >= 0) cc->volume.value = vol / 100.0f;

  // Vollflächiger Dim-Hintergrund
  cc->bg_rect = wlr_scene_rect_create(cc->tree, sw, sh,
      (float[4]){ 0.0f, 0.0f, 0.0f, 0.55f });

  // Geometrie: bei kleinen Bildschirmen kompaktere Kacheln
  int tile_h = sh < 700 ? 44 : 52;
  int row_gap = sh < 700 ? 8 : 10;
  int ty = 10;
  int info_y = ty + 4 * (tile_h + row_gap) + 6;
  int slider_base = info_y + 24;
  int btn_h = 34;
  int btn_row_y = slider_base + 100;

  // Panel — große Kapsel wie iOS (36px oben gerundet, glasig 0.78)
  cc->panel_h = btn_row_y + btn_h + 16;
  if (cc->panel_h > sh - 20) cc->panel_h = sh - 20;
  if (cc->panel_h < 300) cc->panel_h = 300;
  struct phone_theme *theme = server->theme;
  struct wlr_buffer *pbg = phone_render_glass(server->allocator,
      sw, cc->panel_h, 32,
      theme ? theme->panel_bg[0] : 0.14f,
      theme ? theme->panel_bg[1] : 0.14f,
      theme ? theme->panel_bg[2] : 0.16f, 0.78f);
  if (pbg) {
    cc->panel_buf = wlr_scene_buffer_create(cc->tree, pbg);
    wlr_scene_node_set_position(&cc->panel_buf->node, 0, 0);
    wlr_buffer_drop(pbg);
  }
  // tiny drag handle (like iPhone sheet)
  {
    struct wlr_buffer *handle = phone_render_pill(server->allocator, 44, 5, 1,1,1,0.32f);
    if (handle) {
      struct wlr_scene_buffer *hb = wlr_scene_buffer_create(cc->tree, handle);
      wlr_scene_node_set_position(&hb->node, (sw-44)/2, 8);
      wlr_buffer_drop(handle);
    }
  }

  // Kachel-Grid (2 Spalten, 4 Reihen)
  int tile_w = (sw - 2 * CC_PADDING - CC_GAP) / CC_TILE_COLS;
  for (int i = 0; i < CC_NUM_TILES; i++) {
    int col = i % CC_TILE_COLS;
    int row = i / CC_TILE_COLS;
    int x = CC_PADDING + col * (tile_w + CC_GAP);
    int y = ty + row * (tile_h + row_gap);
    struct phone_cc_tile *t = &cc->tiles[i];
    t->x = x; t->y = y; t->w = tile_w; t->h = tile_h;
    cc_build_tile(cc, i);
  }

  // Batterie + WLAN-Zeile über den Slidern
  char info[64];
  char battery[24], tstr[16];
  cc_read_battery(battery, sizeof(battery));
  cc_read_time(tstr, sizeof(tstr));
  snprintf(info, sizeof(info), "%s    %s", tstr, battery);
  struct wlr_buffer *info_buf = phone_render_text(server->allocator,
      info, sw - 2 * CC_PADDING, 20, 11,
      theme ? theme->fg[0] : 0.9f,
      theme ? theme->fg[1] : 0.9f,
      theme ? theme->fg[2] : 0.9f);
  if (info_buf) {
    cc->info_buf = wlr_scene_buffer_create(cc->tree, info_buf);
    wlr_scene_node_set_position(&cc->info_buf->node, CC_PADDING, info_y);
    wlr_buffer_drop(info_buf);
  }

  // Slider für Helligkeit & Lautstärke
  cc_build_slider(cc, &cc->brightness, "Helligkeit", sw, slider_base);
  cc_build_slider(cc, &cc->volume, "Lautstaerke", sw, slider_base + 50);

  // Power-/Sperr-/Einstellungen-Buttons unten
  const char *pow_labels[CC_POWER_BTNS] = { "Power", "Lock", "Sleep", "Einst." };
  int btn_w = (sw - 2 * CC_PADDING - 3 * CC_GAP) / CC_POWER_BTNS;
  int pow_colors[CC_POWER_BTNS][3] = {
    { 0.55f, 0.20f, 0.20f },
    { 0.20f, 0.45f, 0.90f },
    { 0.85f, 0.55f, 0.15f },
    { 0.40f, 0.40f, 0.46f },
  };
  for (int i = 0; i < CC_POWER_BTNS; i++) {
    int x = CC_PADDING + i * (btn_w + CC_GAP);
    struct phone_cc_tile *t = &cc->power_btns[i];
    t->x = x; t->y = btn_row_y; t->w = btn_w; t->h = btn_h;
    struct wlr_buffer *buf = phone_render_rounded_rect(server->allocator,
        btn_w, btn_h, 10,
        pow_colors[i][0], pow_colors[i][1],
        pow_colors[i][2], 0.95f);
    if (buf) {
      t->bg = wlr_scene_buffer_create(cc->tree, buf);
      wlr_scene_node_set_position(&t->bg->node, x, btn_row_y);
      wlr_buffer_drop(buf);
    }
    struct wlr_buffer *pb = phone_render_text(server->allocator,
        pow_labels[i], btn_w, btn_h, 12, 1.0f, 1.0f, 1.0f);
    if (pb) {
      t->label = wlr_scene_buffer_create(cc->tree, pb);
      wlr_scene_node_set_position(&t->label->node, x, btn_row_y);
      wlr_buffer_drop(pb);
    }
  }

  wlr_scene_node_set_enabled(&cc->tree->node, true);
  cc->visible = true;
  anim_cancel(cc->tree);
  anim_slide_y(server, cc->tree, -cc->panel_h, 0, ANIM_DURATION_MS, cc_show_done, cc);
}

void control_center_hide(struct phone_control_center *cc) {
  if (!cc || !cc->visible) return;
  cc->visible = false;
  if (cc->server) cc->server->cc_drag_slider = NULL;
  anim_slide_y(cc->server, cc->tree, 0, -cc->panel_h, ANIM_DURATION_SHORT, cc_hide_done, cc);
  // keep nodes until animation finishes; cc_clear will be called on next show or delayed
}

// Control Center ein-/ausblenden (Umschalter)
void control_center_toggle(struct phone_control_center *cc) {
  if (!cc) return;
  if (cc->visible) control_center_hide(cc);
  else control_center_show(cc);
}

// Screenshot mit grim aufnehmen (auch für die Druck-Taste nutzbar)
void phone_screenshot(struct phone_server *server) {
  char cmd[256];
  const char *home = getenv("HOME");
  if (!home || !home[0]) home = "/tmp";
  snprintf(cmd, sizeof(cmd),
      "grim \"%s/Bilder/screenshot-$(date +%%s).png\" 2>/dev/null || "
      "grim \"%s/screenshot-$(date +%%s).png\"",
      home, home);
  pid_t pid = fork();
  if (pid == 0) {
    setsid();
    execlp("/bin/sh", "sh", "-c", cmd, NULL);
    _exit(1);
  }
  notify_post(server, "phone-wm", "Screenshot",
      "Screenshot wurde aufgenommen.");
}

// Slider-Wert setzen, neu zeichnen und OSD anzeigen (für Tap & Zieh-Geste)
void control_center_slider_set(struct phone_control_center *cc,
                               struct phone_cc_slider *s, float value) {
  if (!cc || !s) return;
  if (value < 0.0f) value = 0.0f;
  if (value > 1.0f) value = 1.0f;
  s->value = value;
  int pct = (int)(value * 100.0f + 0.5f);
  bool is_brightness = (s == &cc->brightness);
  if (is_brightness) hw_backlight_set(pct);
  else hw_volume_set(pct);
  cc_update_slider(s);
  osd_show(cc->server, is_brightness ? "Helligkeit" : "Lautstaerke", pct);
}

// Tap im Control Center verarbeiten (Kacheln schalten, Slider setzen)
void control_center_handle_tap(struct phone_control_center *cc, double x, double y) {
  if (!cc || !cc->visible) return;

  // Tap außerhalb des Panels → schließen
  if (y > cc->panel_h) {
    control_center_hide(cc);
    return;
  }

  // Kacheln prüfen (ein/aus schalten)
  for (int i = 0; i < CC_NUM_TILES; i++) {
    struct phone_cc_tile *t = &cc->tiles[i];
    if (x >= t->x && x < t->x + t->w && y >= t->y && y < t->y + t->h) {
      bool want = !t->active;
      switch (i) {
      case CC_TILE_WIFI:
        hw_wifi_set(want);
        break;
      case CC_TILE_BLUETOOTH:
        hw_bluetooth_set(want);
        break;
      case CC_TILE_AIRPLANE:
        hw_airplane_set(want);
        break;
      case CC_TILE_FLASHLIGHT:
        if (hw_torch_state() < 0) {
          wlr_log(WLR_INFO, "%s", "Control center: flashlight not available");
          return;
        }
        hw_torch_set(want);
        break;
      case CC_TILE_NIGHTLIGHT:
        night_set(cc->server, want);
        notify_post(cc->server, "phone-wm", "Nachtmodus",
            want ? "Nachtmodus aktiviert" : "Nachtmodus deaktiviert");
        break;
      case CC_TILE_SCREENSHOT:
        phone_screenshot(cc->server);
        control_center_hide(cc);
        return;
      case CC_TILE_DND:
        cc->server->dnd_enabled = want;
        if (want && cc->server->toast && cc->server->toast->visible)
          toast_hide(cc->server->toast);
        notify_post(cc->server, "phone-wm", "Nicht stoeren",
            want ? "Nicht stoeren aktiv" : "Nicht stoeren aus");
        break;
      case CC_TILE_SILENT:
        cc->server->silent = want;
        hw_volume_set(want ? 0 : 50);
        break;
      }
      t->active = want;
      cc_recolor_tile(cc, i);
      wlr_log(WLR_INFO, "Control center: %s %s",
          cc_tile_label(i), t->active ? "on" : "off");
      if (i == CC_TILE_AIRPLANE) {
        cc->tiles[CC_TILE_WIFI].active = hw_wifi_state() > 0;
        cc->tiles[CC_TILE_BLUETOOTH].active = hw_bluetooth_state() > 0;
        cc_recolor_tile(cc, CC_TILE_WIFI);
        cc_recolor_tile(cc, CC_TILE_BLUETOOTH);
      }
      return;
    }
  }

  // Power-/Sperr-/Einstellungen-Buttons prüfen
  for (int i = 0; i < CC_POWER_BTNS; i++) {
    struct phone_cc_tile *t = &cc->power_btns[i];
    if (!t->bg) break;
    if (x >= t->x && x < t->x + t->w && y >= t->y && y < t->y + t->h) {
      if (i == 0) { // Power → Power-Menü öffnen
        control_center_hide(cc);
        power_show(cc->server->power);
      } else if (i == 1) { // Lock → Sperrbildschirm
        control_center_hide(cc);
        lock_show(cc->server->lock);
      } else if (i == 2) { // Sleep → System schlafen legen
        notify_post(cc->server, "phone-wm", "Ruhezustand",
            "System wird in den Ruhezustand versetzt.");
        pid_t pid = fork();
        if (pid == 0) {
          setsid();
          execlp("/bin/sh", "sh", "-c", "systemctl suspend", NULL);
          _exit(1);
        }
        control_center_hide(cc);
      } else { // Einstellungen öffnen
        control_center_hide(cc);
        settings_show(cc->server->settings);
      }
      return;
    }
  }

  // Slider prüfen (Tap auf Track setzt Wert)
  struct phone_cc_slider *sliders[] = { &cc->brightness, &cc->volume };
  for (size_t i = 0; i < sizeof(sliders) / sizeof(sliders[0]); i++) {
    struct phone_cc_slider *s = sliders[i];
    if (x >= s->x && x < s->x + s->w && y >= s->y - 26 && y < s->y + s->h + 10) {
      float frac = (float)(x - s->x) / s->w;
      control_center_slider_set(cc, s, frac);
      return;
    }
  }
}