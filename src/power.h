#ifndef PHONE_WM_POWER_H
#define PHONE_WM_POWER_H

#include <stdbool.h>
#include <wlr/types/wlr_scene.h>

struct phone_server;

#define POWER_NUM_BUTTONS 3

// Das Power-Menü: Ausschalten / Neustart / Ruhezustand
struct phone_power {
  struct phone_server *server;
  struct wlr_scene_tree *tree;
  struct wlr_scene_rect *bg_rect; // Vollflächiger Dim
  bool visible;

  // Buttons
  struct wlr_scene_rect *btn_bg[POWER_NUM_BUTTONS];
  struct wlr_scene_buffer *btn_buf[POWER_NUM_BUTTONS];
  int btn_x, btn_y, btn_w, btn_h; // Gemeinsame Geometrie (untereinander)
};

// Power-Menü initialisieren
void power_init(struct phone_server *server);
// Power-Menü anzeigen / verstecken
void power_show(struct phone_power *power);
void power_hide(struct phone_power *power);
void power_toggle(struct phone_power *power);
// Tap im Power-Menü verarbeiten (Button ausführen, sonst schließen)
void power_handle_tap(struct phone_power *power, double x, double y);

#endif