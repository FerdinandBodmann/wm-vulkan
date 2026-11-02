#define _POSIX_C_SOURCE 200809L
#include "osk.h"
#include "render.h"
#include "server.h"
#include "anim.h"
#include <stdlib.h>
#include <string.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_text_input_v3.h>
#include <wlr/util/log.h>

// Statische Definitionen des Tastatur-Layouts (QWERTY)
// type: 0=Letter, 1=Shift, 2=Backspace, 3=Space, 4=Enter
static const struct {
  const char *lower;
  const char *upper;
  uint32_t keycode;
  int type;
} osk_key_defs[] = {
  // Zeile 0: q w e r t y u i o p
  {"q", "Q", KEY_Q, OSK_TYPE_LETTER},
  {"w", "W", KEY_W, OSK_TYPE_LETTER},
  {"e", "E", KEY_E, OSK_TYPE_LETTER},
  {"r", "R", KEY_R, OSK_TYPE_LETTER},
  {"t", "T", KEY_T, OSK_TYPE_LETTER},
  {"y", "Y", KEY_Y, OSK_TYPE_LETTER},
  {"u", "U", KEY_U, OSK_TYPE_LETTER},
  {"i", "I", KEY_I, OSK_TYPE_LETTER},
  {"o", "O", KEY_O, OSK_TYPE_LETTER},
  {"p", "P", KEY_P, OSK_TYPE_LETTER},
  // Zeile 1: a s d f g h j k l
  {"a", "A", KEY_A, OSK_TYPE_LETTER},
  {"s", "S", KEY_S, OSK_TYPE_LETTER},
  {"d", "D", KEY_D, OSK_TYPE_LETTER},
  {"f", "F", KEY_F, OSK_TYPE_LETTER},
  {"g", "G", KEY_G, OSK_TYPE_LETTER},
  {"h", "H", KEY_H, OSK_TYPE_LETTER},
  {"j", "J", KEY_J, OSK_TYPE_LETTER},
  {"k", "K", KEY_K, OSK_TYPE_LETTER},
  {"l", "L", KEY_L, OSK_TYPE_LETTER},
  // Zeile 2: Shift z x c v b n m Backspace
  {"\u21E7", NULL, KEY_LEFTSHIFT, OSK_TYPE_SHIFT},
  {"z", "Z", KEY_Z, OSK_TYPE_LETTER},
  {"x", "X", KEY_X, OSK_TYPE_LETTER},
  {"c", "C", KEY_C, OSK_TYPE_LETTER},
  {"v", "V", KEY_V, OSK_TYPE_LETTER},
  {"b", "B", KEY_B, OSK_TYPE_LETTER},
  {"n", "N", KEY_N, OSK_TYPE_LETTER},
  {"m", "M", KEY_M, OSK_TYPE_LETTER},
  {"\u232B", NULL, KEY_BACKSPACE, OSK_TYPE_BACKSPACE},
  // Zeile 3: Leertaste + Enter
  {"space", NULL, KEY_SPACE, OSK_TYPE_SPACE},
  {"Enter", NULL, KEY_ENTER, OSK_TYPE_ENTER},
};

// Keyboard-Impl für unser synthetisches Keyboard (keine LED-Steuerung)
static const struct wlr_keyboard_impl osk_kb_impl = {
  .name = "phone-wm-osk",
};

// Alle Tasten-Scene-Buffer zerstören (z.B. bei Shift-Wechsel neu rendern)
static void osk_destroy_keys(struct phone_osk *osk) {
  for (int i = 0; i < osk->num_keys; i++) {
    if (osk->keys[i].buf) {
      wlr_scene_node_destroy(&osk->keys[i].buf->node);
      osk->keys[i].buf = NULL;
    }
  }
}

// Layout berechnen und alle Tasten-Buffer (neu) erzeugen
static void osk_rebuild(struct phone_osk *osk) {
  const int W = osk->width;
  const int H = osk->height;
  const int pad = 8;

  // Tasten-Buffer zerstören die neu aufgebaut werden
  osk_destroy_keys(osk);

  // Reihenhöhe berechnen (4 Zeilen + 5 Abstände)
  const int row_h = (H - 5 * pad) / 4;

  // Hintergrund-Rechteck anpassen
  wlr_scene_rect_set_size(osk->bg_rect, W, H);

  int idx = 0;

  // Zeile 0: 10 Tasten (q...p)
  int rw0 = (W - 11 * pad) / 10;
  int y = pad;
  for (int i = 0; i < 10; i++, idx++) {
    osk->keys[idx].x = pad + i * (rw0 + pad);
    osk->keys[idx].y = y;
    osk->keys[idx].w = rw0;
    osk->keys[idx].h = row_h;
  }

  // Zeile 1: 9 Tasten (a...l), zentriert
  y += row_h + pad;
  int rw1 = (W - 10 * pad) / 9;
  int x1 = (W - (9 * rw1 + 8 * pad)) / 2;
  for (int i = 0; i < 9; i++, idx++) {
    osk->keys[idx].x = x1 + i * (rw1 + pad);
    osk->keys[idx].y = y;
    osk->keys[idx].w = rw1;
    osk->keys[idx].h = row_h;
  }

  // Zeile 2: Shift(1.5x) + 7 Buchstaben + Backspace(1.5x)
  y += row_h + pad;
  int rw2 = (W - 11 * pad) / 10;
  int sw = rw2 * 3 / 2;
  int x2 = (W - (7 * rw2 + 2 * sw + 8 * pad)) / 2;
  osk->keys[idx].x = x2;
  osk->keys[idx].y = y;
  osk->keys[idx].w = sw;
  osk->keys[idx].h = row_h;
  idx++;
  x2 += sw + pad;
  for (int i = 0; i < 7; i++, idx++) {
    osk->keys[idx].x = x2 + i * (rw2 + pad);
    osk->keys[idx].y = y;
    osk->keys[idx].w = rw2;
    osk->keys[idx].h = row_h;
  }
  osk->keys[idx].x = x2 + 7 * rw2 + 7 * pad;
  osk->keys[idx].y = y;
  osk->keys[idx].w = sw;
  osk->keys[idx].h = row_h;
  idx++;

  // Zeile 3: Leertaste (breit) + Enter
  y += row_h + pad;
  int ew = (W - 4 * pad) * 2 / 5;
  int space_w = W - ew - 3 * pad;
  osk->keys[idx].x = pad;
  osk->keys[idx].y = y;
  osk->keys[idx].w = space_w;
  osk->keys[idx].h = row_h;
  idx++;
  osk->keys[idx].x = space_w + 2 * pad;
  osk->keys[idx].y = y;
  osk->keys[idx].w = ew;
  osk->keys[idx].h = row_h;
  idx++;

  // Tasten-Buffer erzeugen
  for (int i = 0; i < osk->num_keys; i++) {
    struct phone_osk_key *key = &osk->keys[i];
    const char *label = key->lower;
    if (osk->shift_active && key->upper) label = key->upper;

    // Farben je nach Tastenart
    float bg_r = 0.25f, bg_g = 0.25f, bg_b = 0.25f;
    if (key->type != OSK_TYPE_LETTER) {
      bg_r = 0.35f; bg_g = 0.35f; bg_b = 0.35f;
    }
    // Shift-Taste hervorheben wenn aktiv
    if (key->type == OSK_TYPE_SHIFT && osk->shift_active) {
      bg_r = 0.55f; bg_g = 0.55f; bg_b = 0.55f;
    }

    struct wlr_buffer *buf = phone_render_key(osk->server->allocator,
        label, key->w, key->h, 18, bg_r, bg_g, bg_b, 1.0f, 1.0f, 1.0f);
    if (!buf) continue;
    key->buf = wlr_scene_buffer_create(osk->tree, buf);
    wlr_buffer_drop(buf);
    if (!key->buf) continue;
    wlr_scene_node_set_position(&key->buf->node, key->x, key->y);
  }

  osk->built = true;
}

// Tastatur anzeigen/verstecken
static void osk_hide_done(void *d){ struct phone_osk *o=d; if(o && !o->visible) wlr_scene_node_set_enabled(&o->tree->node,false); }
void osk_set_visible(struct phone_osk *osk, bool visible) {
  if (!osk || osk->visible == visible) return;
  osk->visible = visible;
  wlr_log(WLR_INFO, "On-screen keyboard %s", visible ? "SHOWN" : "HIDDEN");
  if (visible) {
    osk->shift_active = false;
    if (!osk->built) osk_rebuild(osk);
    wlr_scene_node_set_enabled(&osk->tree->node, true);
    anim_cancel(osk->tree);
    anim_slide_y(osk->server, osk->tree, osk->height, 0, ANIM_DURATION_SHORT, NULL, NULL);
  } else {
    anim_slide_y(osk->server, osk->tree, 0, osk->height, ANIM_DURATION_SHORT, osk_hide_done, osk);
  }
  update_seat_capabilities(osk->server);
}

// Bildschirmgröße mitteilen, Tastatur unten über der Navigationsleiste anordnen
void osk_update_size(struct phone_osk *osk, int screen_w, int screen_h) {
  if (!osk) return;
  int lw, lh; float lsc; anim_get_logical_size(osk->server, &lw, &lh, &lsc);
  screen_w = lw; screen_h = lh;
  osk->width = screen_w;
  osk->height = anim_scaled(240, lsc);
  int bar_h = osk->server->bar ? osk->server->bar->height : 0;
  osk->pos_x = 0;
  osk->pos_y = screen_h - osk->height - bar_h;
  wlr_scene_node_set_position(&osk->tree->node, osk->pos_x, osk->pos_y);
  osk->built = false;
  osk_rebuild(osk);
}

// Taste an die fokussierte Oberfläche schicken (Keycode + Modifier)
static void osk_send_key(struct phone_osk *osk, struct phone_osk_key *key,
                         uint32_t time) {
  struct wlr_seat *seat = osk->server->seat;
  struct wlr_keyboard_modifiers mods = {0};

  // Shift-Taste: Umschaltzustand togglen
  if (key->type == OSK_TYPE_SHIFT) {
    osk->shift_active = !osk->shift_active;
    osk_rebuild(osk);
    if (osk->shift_active) mods.depressed = osk->shift_mask;
    wlr_seat_keyboard_notify_modifiers(seat, &mods);
    return;
  }

  // Shift-Modifier setzen falls aktiv
  if (osk->shift_active) {
    mods.depressed = osk->shift_mask;
    wlr_seat_keyboard_notify_modifiers(seat, &mods);
  }

  // Tastendruck + Loslassen an die App schicken
  wlr_seat_keyboard_notify_key(seat, time, key->keycode,
                               WL_KEYBOARD_KEY_STATE_PRESSED);
  wlr_seat_keyboard_notify_key(seat, time, key->keycode,
                               WL_KEYBOARD_KEY_STATE_RELEASED);

  // Shift nach einem Buchstaben wieder ausschalten (wie auf Android)
  if (osk->shift_active && key->type == OSK_TYPE_LETTER) {
    osk->shift_active = false;
    osk_rebuild(osk);
    mods.depressed = 0;
    wlr_seat_keyboard_notify_modifiers(seat, &mods);
  }
}

// Tap auf der Tastatur verarbeiten. true = Tap verbraucht (kommt nicht zur App)
bool osk_handle_tap(struct phone_osk *osk, double x, double y, uint32_t time) {
  if (!osk || !osk->visible) return false;

  // In Layout-Koordinaten relativ zur Tastatur umrechnen
  double lx = x - osk->pos_x;
  double ly = y - osk->pos_y;
  if (lx < 0 || ly < 0 || lx >= osk->width || ly >= osk->height) return false;

  // Prüfen ob eine Taste getroffen wurde
  for (int i = 0; i < osk->num_keys; i++) {
    struct phone_osk_key *key = &osk->keys[i];
    if (lx >= key->x && lx < key->x + key->w &&
        ly >= key->y && ly < key->y + key->h) {
      wlr_log(WLR_DEBUG, "OSK key '%s' (%d) at %.0f,%.0f",
              key->lower, key->keycode, lx, ly);
      osk_send_key(osk, key, time);
      return true;
    }
  }

  // Tap im Tastaturbereich aber außerhalb einer Taste: trotzdem verbrauchen
  return true;
}

// --- text-input-v3 Handling ------------------------------------------

// Textfeld wurde fokussiert: On-Screen-Tastatur einblenden
static void text_input_enable(struct wl_listener *listener, void *data) {
  (void)data;
  struct phone_text_input *pti = wl_container_of(listener, pti, enable);
  // Textfeld fokussiert -> Tastatur einblenden
  osk_set_visible(pti->server->osk, true);
}

// Textfeld wurde verlassen: On-Screen-Tastatur ausblenden
static void text_input_disable(struct wl_listener *listener, void *data) {
  (void)data;
  struct phone_text_input *pti = wl_container_of(listener, pti, disable);
  // Textfeld verlassen -> Tastatur ausblenden
  osk_set_visible(pti->server->osk, false);
}

// text_input_v3-Objekt zerstört: Listener entfernen und Speicher freigeben
static void text_input_destroy(struct wl_listener *listener, void *data) {
  (void)data;
  struct phone_text_input *pti = wl_container_of(listener, pti, destroy);
  wl_list_remove(&pti->link);
  wl_list_remove(&pti->enable.link);
  wl_list_remove(&pti->disable.link);
  wl_list_remove(&pti->destroy.link);
  free(pti);
}

// Neues text_input_v3-Objekt registrieren (enable/disable steuern die Tastatur)
void osk_new_text_input(struct phone_server *server,
                        struct wlr_text_input_v3 *text_input) {
  struct phone_text_input *pti = calloc(1, sizeof(struct phone_text_input));
  if (!pti) return;
  pti->server = server;
  pti->text_input = text_input;
  pti->enable.notify = text_input_enable;
  pti->disable.notify = text_input_disable;
  pti->destroy.notify = text_input_destroy;
  wl_signal_add(&text_input->events.enable, &pti->enable);
  wl_signal_add(&text_input->events.disable, &pti->disable);
  wl_signal_add(&text_input->events.destroy, &pti->destroy);
  wl_list_insert(&server->text_inputs, &pti->link);
}

// On-Screen-Tastatur initialisieren: Scene-Tree, XKB-Keymap und Seat-Keyboard
void osk_init(struct phone_server *server) {
  struct phone_osk *osk = calloc(1, sizeof(struct phone_osk));
  if (!osk) return;
  server->osk = osk;
  osk->server = server;
  osk->height = 240;
  osk->visible = false;
  osk->built = false;
  osk->shift_active = false;

  // Layout-Tasten aus den statischen Definitionen übernehmen
  osk->num_keys = (int)(sizeof(osk_key_defs) / sizeof(osk_key_defs[0]));
  for (int i = 0; i < osk->num_keys; i++) {
    osk->keys[i].lower = osk_key_defs[i].lower;
    osk->keys[i].upper = osk_key_defs[i].upper;
    osk->keys[i].keycode = osk_key_defs[i].keycode;
    osk->keys[i].type = osk_key_defs[i].type;
  }

  // Scene-Baum im Overlay-Layer anlegen (ganz vorne)
  osk->tree = wlr_scene_tree_create(server->layer_overlay_tree);
  osk->bg_rect = wlr_scene_rect_create(osk->tree, 10, osk->height,
      (float[4]){0.14f, 0.14f, 0.14f, 0.98f});

  // Tastatur zunächst unsichtbar
  wlr_scene_node_set_enabled(&osk->tree->node, false);

  // XKB-Keymap erzeugen (Standard-Layout) und am Seat registrieren.
  // Dadurch bekommen auch Clients ohne physische Tastatur eine Keymap und
  // können die Keycodes der On-Screen-Tastatur dekodieren.
  struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (ctx) {
    struct xkb_rule_names rules = {0};
    osk->keymap = xkb_keymap_new_from_names(ctx, &rules,
                                            XKB_KEYMAP_COMPILE_NO_FLAGS);
    xkb_context_unref(ctx);
  }
  if (osk->keymap) {
    xkb_mod_index_t shift_idx =
        xkb_keymap_mod_get_index(osk->keymap, XKB_MOD_NAME_SHIFT);
    osk->shift_mask = shift_idx != XKB_MOD_INVALID
        ? (xkb_mod_mask_t)1 << shift_idx : 0;
    wlr_keyboard_init(&osk->keyboard, &osk_kb_impl, "phone-wm-osk");
    wlr_keyboard_set_keymap(&osk->keyboard, osk->keymap);
    wlr_seat_set_keyboard(server->seat, &osk->keyboard);
  }
}
