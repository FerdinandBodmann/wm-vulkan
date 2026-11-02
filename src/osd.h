#ifndef PHONE_WM_OSD_H
#define PHONE_WM_OSD_H

#include <wayland-server-core.h>
#include <wlr/types/wlr_scene.h>

struct phone_server;

// On-Screen-Display (OSD): kurzes Overlay für Lautstärke-/Helligkeitsänderungen.
// Zeigt Label + Fortschrittsbalken und blendet sich automatisch aus.
struct phone_osd {
  struct phone_server *server;
  struct wlr_scene_tree *tree;
  struct wlr_scene_buffer *bg;   // Abgerundeter Hintergrund
  struct wlr_scene_buffer *text; // Beschriftung (z.B. "Lautstaerke 70%")
  struct wlr_scene_rect *track;  // Fortschritts-Hintergrund
  struct wlr_scene_rect *fill;   // Gefüllter Teil (Akzentfarbe)
  struct wl_event_source *timer; // Auto-Ausblenden
  bool visible;
  int x, y, w, h;
};

// OSD-System initialisieren (Scene-Tree im Overlay)
void osd_init(struct phone_server *server);
// OSD anzeigen (ersetzt den aktuellen), pct = 0..100
void osd_show(struct phone_server *server, const char *label, int pct);
// OSD ausblenden und Nodes entfernen
void osd_hide(struct phone_osd *osd);

#endif