#ifndef PHONE_WM_OSK_H
#define PHONE_WM_OSK_H

#include <linux/input-event-codes.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_scene.h>
#include <xkbcommon/xkbcommon.h>

struct phone_server;
struct wlr_text_input_v3;

// Arten von Tasten auf der On-Screen-Tastatur
enum osk_key_type {
  OSK_TYPE_LETTER,     // Normaler Buchstabe
  OSK_TYPE_SHIFT,      // Umschalttaste (⇧)
  OSK_TYPE_BACKSPACE,  // Rücktaste (⌫)
  OSK_TYPE_SPACE,      // Leertaste
  OSK_TYPE_ENTER,      // Eingabetaste
};

#define OSK_NUM_KEYS 32 // Maximale Anzahl Tasten (30 Layout-Tasten + Reserve)

// Eine einzelne Taste der On-Screen-Tastatur
struct phone_osk_key {
  int x, y, w, h;        // Position & Größe (relativ zum Tastatur-Baum)
  const char *lower;     // Beschriftung normal (z.B. "q")
  const char *upper;     // Beschriftung mit Shift (z.B. "Q"), NULL wenn gleich
  uint32_t keycode;      // Evdev-Keycode der an die App geschickt wird
  int type;              // enum osk_key_type
  struct wlr_scene_buffer *buf; // Scene-Buffer für das Tastaturbild
};

// On-Screen-Tastatur (wie SwiftKey auf Android)
struct phone_osk {
  struct phone_server *server;  // Zeiger auf den Server
  struct wlr_scene_tree *tree;  // Scene-Baum der Tastatur
  struct wlr_scene_rect *bg_rect; // Hintergrund-Rechteck
  int width, height;            // Tastaturgröße in Pixeln
  int pos_x, pos_y;             // Position des Tastatur-Baums im Layout
  bool visible;                 // Ob die Tastatur gerade angezeigt wird
  bool built;                   // Ob die Tasten-Buffer schon erstellt wurden
  bool shift_active;            // Ob Shift gerade aktiv ist
  struct xkb_keymap *keymap;    // XKB-Keymap für die Tastatur
  struct wlr_keyboard keyboard; // Synthetisches Keyboard für die Keymap am Seat
  xkb_mod_mask_t shift_mask;    // Bitmaske für die Shift-Modifier
  struct phone_osk_key keys[OSK_NUM_KEYS]; // Alle Tasten
  int num_keys;                 // Anzahl aktiver Tasten
};

// On-Screen-Tastatur initialisieren (Scene-Baum, Keymap, Seat-Keyboard)
void osk_init(struct phone_server *server);

// Neues text_input_v3-Objekt registrieren (zeigt/versteckt die Tastatur)
void osk_new_text_input(struct phone_server *server,
                        struct wlr_text_input_v3 *text_input);

// Tastatur ein-/ausblenden
void osk_set_visible(struct phone_osk *osk, bool visible);

// Größe des Bildschirms bekannt geben (Tastatur unten positionieren)
void osk_update_size(struct phone_osk *osk, int screen_w, int screen_h);

// Tap auf der Tastatur verarbeiten. Gibt true zurück wenn der Tap verbraucht wurde.
bool osk_handle_tap(struct phone_osk *osk, double x, double y, uint32_t time);

#endif
