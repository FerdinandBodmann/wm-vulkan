#ifndef PHONE_WM_LOCK_H
#define PHONE_WM_LOCK_H

#include <stdbool.h>
#include <wlr/types/wlr_scene.h>

struct phone_server;

// Der Sperrbildschirm (swipe up zum Entsperren)
struct phone_lock {
  struct phone_server *server;
  struct wlr_scene_tree *tree;
  struct wlr_scene_rect *bg_rect;       // Vollflächiger Hintergrund
  struct wlr_scene_buffer *clock_buf;   // Große Uhrzeit
  struct wlr_scene_buffer *hint_buf;    // "Zum Entsperren hochwischen"
  struct wlr_scene_buffer *bat_buf;     // Akkuzeile
  bool visible;
};

// Sperrbildschirm initialisieren
void lock_init(struct phone_server *server);
// Sperrbildschirm anzeigen / verstecken
void lock_show(struct phone_lock *lock);
void lock_hide(struct phone_lock *lock);
void lock_toggle(struct phone_lock *lock);
// Tap auf dem Sperrbildschirm (verbraucht Eingaben)
void lock_handle_tap(struct phone_lock *lock, double x, double y);

#endif