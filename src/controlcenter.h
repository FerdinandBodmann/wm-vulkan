#ifndef PHONE_WM_CONTROLCENTER_H
#define PHONE_WM_CONTROLCENTER_H

#include <wlr/types/wlr_scene.h>

#define CC_NUM_TILES 8
#define CC_TILE_COLS 2
#define CC_POWER_BTNS 4

struct phone_server;

// Die Quick-Settings Kacheln (wie auf Android)
enum phone_cc_tile_id {
  CC_TILE_WIFI,
  CC_TILE_BLUETOOTH,
  CC_TILE_AIRPLANE,
  CC_TILE_FLASHLIGHT,
  CC_TILE_NIGHTLIGHT,
  CC_TILE_SCREENSHOT,
  CC_TILE_DND,
  CC_TILE_SILENT,
};

struct phone_cc_tile {
  struct wlr_scene_buffer *bg;    // Kachel-Hintergrund (abgerundet, Farbe = Zustand)
  struct wlr_scene_buffer *label; // Kachel-Text
  bool active;                    // Ein/Aus-Zustand
  int x, y, w, h;
};

struct phone_cc_slider {
  struct wlr_scene_rect *track; // Hintergrund-Balken
  struct wlr_scene_rect *fill;  // gefüllter Teil (zeigt Wert)
  struct wlr_scene_buffer *label; // Beschriftung
  int x, y, w, h;
  float value; // 0.0 .. 1.0
};

struct phone_control_center {
  struct phone_server *server;
  struct wlr_scene_tree *tree;
  struct wlr_scene_rect *bg_rect;    // Vollflächiger Dim
  struct wlr_scene_buffer *panel_buf; // Panel-Hintergrund (abgerundet)
  struct wlr_scene_buffer *info_buf; // Uhrzeit / Akku-Zeile

  bool visible;
  int panel_h;
  int screen_w, screen_h;

  struct phone_cc_tile tiles[CC_NUM_TILES];
  struct phone_cc_slider brightness;
  struct phone_cc_slider volume;

  // Power-/Sperr-/Einstellungen-Buttons unten
  struct phone_cc_tile power_btns[CC_POWER_BTNS];
};

// Control Center initialisieren (Scene-Tree im Overlay anlegen)
void control_center_init(struct phone_server *server);
// Control Center öffnen: Panel mit Kacheln und Slidern aufbauen
void control_center_show(struct phone_control_center *cc);
// Control Center schließen und alle Scene-Nodes entfernen
void control_center_hide(struct phone_control_center *cc);
// Control Center ein-/ausblenden (Umschalter)
void control_center_toggle(struct phone_control_center *cc);
// Tap im Control Center verarbeiten (Kacheln schalten, Slider setzen)
void control_center_handle_tap(struct phone_control_center *cc, double x, double y);
// Slider-Wert setzen und neu zeichnen (für Zieh-Gesten) + OSD anzeigen
void control_center_slider_set(struct phone_control_center *cc,
                               struct phone_cc_slider *s, float value);

#endif