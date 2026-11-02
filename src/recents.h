#ifndef PHONE_WM_RECENTS_H
#define PHONE_WM_RECENTS_H

#include <wlr/types/wlr_scene.h>

#define RECENTS_MAX_CARDS 16

struct phone_server;
struct phone_view;

struct phone_recents_card {
  struct phone_view *view;
  struct wlr_scene_rect *bg_rect;
  struct wlr_scene_buffer *icon_buf;   // App-Icon statt Text
  struct wlr_buffer *icon_wlr_buf;
  struct wlr_scene_rect *close_bg;     // Close-Button Hintergrund
  struct wlr_scene_buffer *close_buf;  // X-Icon
  struct wlr_buffer *close_wlr_buf;
  int x, y, w, h;
  int close_x, close_y, close_w, close_h; // Close-Button Geometrie
};

struct phone_recents {
  struct phone_server *server;
  struct wlr_scene_tree *tree;
  struct wlr_scene_rect *bg_rect;
  bool visible;
  int num_cards;
  struct phone_recents_card cards[RECENTS_MAX_CARDS];

  // "Alle schliessen" Button
  struct wlr_scene_rect *close_all_bg;
  struct wlr_scene_buffer *close_all_buf;
  struct wlr_buffer *close_all_wlr_buf;
  int close_all_x, close_all_y, close_all_w, close_all_h;
};

// Recents-Übersicht initialisieren (Scene-Tree im Overlay anlegen)
void recents_init(struct phone_server *server);
// Recents-Übersicht anzeigen: Karten aller offenen Apps aufbauen
void recents_show(struct phone_recents *recents);
// Recents-Übersicht ausblenden
void recents_hide(struct phone_recents *recents);
// Tap in der Recents-Übersicht verarbeiten (Karte schließen/öffnen)
void recents_handle_tap(struct phone_recents *recents, double x, double y);

#endif
