#ifndef PHONE_WM_TOAST_H
#define PHONE_WM_TOAST_H

#include <wayland-server-core.h>
#include <wlr/types/wlr_scene.h>

struct phone_server;

// Kurzes Toast-Banner am oberen Bildschirmrand für eingehende Benachrichtigungen.
// Erscheint unabhängig vom Notify-Shade und verschwindet nach ein paar Sekunden.
struct phone_toast {
  struct phone_server *server;
  struct wlr_scene_tree *tree;
  struct wlr_scene_buffer *bg;   // Abgerundeter Hintergrund
  struct wlr_scene_buffer *text; // App-Kürzel + Titel
  struct wl_event_source *timer; // Auto-Ausblenden
  bool visible;
  int x, y, w, h;
};

// Toast-System initialisieren (Scene-Tree im Overlay)
void toast_init(struct phone_server *server);
// Toast anzeigen (ersetzt den aktuellen, falls einer sichtbar ist)
void toast_show(struct phone_toast *toast, const char *app, const char *title);
// Toast ausblenden und Nodes entfernen
void toast_hide(struct phone_toast *toast);

#endif