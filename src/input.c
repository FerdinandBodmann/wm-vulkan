#include "lock.h"
#include "notify.h"
#include "power.h"
#include "render.h"
#include "server.h"
#include "settings.h"
#include <linux/input-event-codes.h>
#include <math.h>
#include <stdlib.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

// Tastatur-Device zerstört: Listener entfernen und Speicher freigeben
static void keyboard_destroy(struct wl_listener *listener, void *data) {
  // Keyboard-Objekt holen, das zerstört wird
  struct phone_keyboard *keyboard = wl_container_of(listener, keyboard, destroy);
  // Server-Referenz holen
  struct phone_server *server = keyboard->server;
  // Alle Listener aus den Listen entfernen
  wl_list_remove(&keyboard->link);
  wl_list_remove(&keyboard->destroy.link);
  wl_list_remove(&keyboard->key.link);
  wl_list_remove(&keyboard->modifiers.link);
  // Speicher freigeben
  free(keyboard);
  // Seat-Fähigkeiten aktualisieren, weil sich was geändert hat
  update_seat_capabilities(server);
}

// Tastendruck verarbeiten: Shortcuts prüfen, Key an die fokussierte App senden
static void keyboard_key(struct wl_listener *listener, void *data) {
  // Keyboard aus dem Listener holen
  struct phone_keyboard *keyboard = wl_container_of(listener, keyboard, key);
  struct phone_server *server = keyboard->server;
  // Key-Event-Daten extrahieren
  struct wlr_keyboard_key_event *event = data;
  struct wlr_seat *seat = server->seat;

  // Modifier-Tasten manuell tracken (der WM setzt keine XKB-Keymap, daher
  // ist keyboard->wlr_keyboard->xkb_state immer NULL)
  if (event->keycode == KEY_LEFTCTRL || event->keycode == KEY_RIGHTCTRL) {
    server->ctrl_pressed = (event->state == WL_KEYBOARD_KEY_STATE_PRESSED);
  } else if (event->keycode == KEY_LEFTSHIFT || event->keycode == KEY_RIGHTSHIFT) {
    server->shift_pressed = (event->state == WL_KEYBOARD_KEY_STATE_PRESSED);
  }

  // Nur bei Tastendruck (nicht Loslassen) Shortcuts prüfen
  if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
    // Auto-Sperre: Leerlaufzeit aktualisieren
    server->last_input_time = time(NULL);

    // Strg + Shift + L = Maus-Modus ein-/ausschalten
    if (event->keycode == KEY_L && server->ctrl_pressed && server->shift_pressed) {
      pointer_toggle(server); // Maus-Modus umschalten
      return; // Shortcut nicht an die App weiterleiten
    }

    // POWER-Taste → Sperrbildschirm
    if (event->keycode == KEY_POWER) {
      if (server->lock) lock_show(server->lock);
      return;
    }

    // Druck-/SYSRQ-Taste → Screenshot
    if (event->keycode == KEY_SYSRQ) {
      phone_screenshot(server);
      return;
    }

    // Launcher-Suche: Tasten in die Suchleiste geben, wenn die Schublade offen ist
    if (server->launcher && server->launcher->visible && !server->ctrl_pressed) {
      uint32_t code = event->keycode;
      if ((code >= KEY_A && code <= KEY_Z) ||
          (code >= KEY_1 && code <= KEY_0) ||
          code == KEY_BACKSPACE || code == KEY_SPACE || code == KEY_ENTER ||
          code == KEY_KPENTER || code == KEY_ESC || code == KEY_DOT ||
          code == KEY_MINUS) {
        launcher_search_key(server->launcher, code);
        return; // Nicht an die App weiterleiten
      }
    }
  }

  // Prüfen ob irgendwo fokussiert ist, um Tastendruck weiterzuleiten
  struct wlr_surface *focused = seat->keyboard_state.focused_surface;
  if (focused) {
    // Tastendruck an die fokussierte Oberfläche schicken
    wlr_seat_keyboard_notify_key(seat, event->time_msec,
                                 event->keycode, event->state);
  }
}

// Modifier-Änderungen (Shift, Ctrl, Alt) an die fokussierte Oberfläche senden
static void keyboard_modifiers(struct wl_listener *listener, void *data) {
  // Keyboard aus dem Listener holen
  struct phone_keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);
  struct phone_server *server = keyboard->server;

  // Aktuelles Keyboard am Seat setzen
  wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);

  // Nur weiterleiten wenn irgendwo fokussiert ist
  struct wlr_surface *focused = server->seat->keyboard_state.focused_surface;
  if (focused) {
    // Modifier-Änderungen (Shift, Ctrl etc.) an die Oberfläche schicken
    wlr_seat_keyboard_notify_modifiers(server->seat,
        &keyboard->wlr_keyboard->modifiers);
  }
}

// Callbacks für die Keyboard-Events registrieren
void input_handle_keyboard(struct phone_keyboard *keyboard) {
  keyboard->destroy.notify = keyboard_destroy;
  keyboard->key.notify = keyboard_key;
  keyboard->modifiers.notify = keyboard_modifiers;
}

// Touch-Device zerstört: Listener entfernen und Speicher freigeben
static void touch_destroy(struct wl_listener *listener, void *data) {
  // Touch-Objekt das zerstört wird
  struct phone_touch *touch = wl_container_of(listener, touch, destroy);
  struct phone_server *server = touch->server;
  // Alle Listener ordentlich entfernen
  wl_list_remove(&touch->link);
  wl_list_remove(&touch->destroy.link);
  wl_list_remove(&touch->down.link);
  wl_list_remove(&touch->up.link);
  wl_list_remove(&touch->motion.link);
  free(touch);
  // Seat updaten weil Touch weg ist
  update_seat_capabilities(server);
}

// Prüft welche Oberfläche unter dem Touch-Punkt liegt
static void touch_get_surface_at(struct phone_server *server,
                                  double lx, double ly,
                                  struct wlr_surface **surface,
                                  double *sx, double *sy) {
  // Standardwert: keine Oberfläche gefunden
  *surface = NULL;

  // Szene an der Position abfragen
  struct wlr_scene_node *node = wlr_scene_node_at(
      &server->scene->tree.node, lx, ly, sx, sy);
  if (!node) return;
  // Nur Buffer-Nodes haben eine Oberfläche
  if (node->type != WLR_SCENE_NODE_BUFFER) return;

  // Node in Scene-Buffer umwandeln
  struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);

  // Prüfen ob das eine Scene-Surface ist
  struct wlr_scene_surface *scene_surface =
      wlr_scene_surface_try_from_buffer(scene_buffer);
  if (!scene_surface) return;

  // Oberfläche zurückgeben
  *surface = scene_surface->surface;
}

// Absolute Touch-Koordinaten in Layout-Koordinaten umrechnen
static void touch_absolute_to_layout(struct phone_server *server,
                                      double x, double y,
                                      double *lx, double *ly) {
  // Layout-Box holen um Größe zu kennen
  struct wlr_box box;
  wlr_output_layout_get_box(server->output_layout, NULL, &box);
  // Normalisierte 0..1 Koordinaten in echte Pixel umrechnen
  *lx = box.x + x * box.width;
  *ly = box.y + y * box.height;
}

// Finger berührt den Bildschirm: Gesture starten und Touch-Down melden
static void touch_down(struct wl_listener *listener, void *data) {
  // Touch-Objekt und Event-Daten holen
  struct phone_touch *touch = wl_container_of(listener, touch, down);
  struct phone_server *server = touch->server;
  struct wlr_touch_down_event *event = data;

  // Absolute Koordinaten in Layout-Koordinaten umrechnen
  double lx, ly;
  touch_absolute_to_layout(server, event->x, event->y, &lx, &ly);

  // Auto-Sperre: Leerlaufzeit aktualisieren
  server->last_input_time = time(NULL);

  // Control Center Slider-Zieh-Geste starten, falls Finger auf einem Slider
  if (server->control_center && server->control_center->visible) {
    struct phone_cc_slider *sliders[] = {
      &server->control_center->brightness,
      &server->control_center->volume
    };
    for (size_t i = 0; i < sizeof(sliders) / sizeof(sliders[0]); i++) {
      struct phone_cc_slider *s = sliders[i];
      if (lx >= s->x && lx < s->x + s->w && ly >= s->y - 26 && ly < s->y + s->h + 10) {
        server->cc_drag_slider = s;
        break;
      }
    }
  }

  // Gesture-Tracking starten
  gesture_begin(&server->current_gesture, lx, ly,
                event->time_msec, event->touch_id);

  // Welche Oberfläche liegt unter dem Finger?
  struct wlr_surface *surface = NULL;
  double sx, sy;
  touch_get_surface_at(server, lx, ly, &surface, &sx, &sy);

  if (surface) {
    // Touch-Down an die betroffene Oberfläche melden
    wlr_seat_touch_notify_down(server->seat, surface, event->time_msec,
                               event->touch_id, sx, sy);
    // Frame-Event schicken damit das alles durchkommt
    wlr_seat_touch_notify_frame(server->seat);
  }
}

// UI-Tap an der Position verarbeiten (gleiche Logik für Touch und Maus)
static void handle_ui_tap(struct phone_server *server, double x, double y,
                          uint32_t time) {
  // Zuerst den Sperrbildschirm prüfen (oberstes Overlay, verbraucht alles)
  if (server->lock && server->lock->visible) {
    lock_handle_tap(server->lock, x, y);
    return;
  }

  // Dann das Power-Menü
  if (server->power && server->power->visible) {
    power_handle_tap(server->power, x, y);
    return;
  }

  // Dann die On-Screen-Tastatur prüfen (liegt ganz oben)
  if (server->osk && server->osk->visible) {
    if (osk_handle_tap(server->osk, x, y, time)) return;
  }

  // Dann die Einstellungen-Seite prüfen
  if (server->settings && server->settings->visible) {
    settings_handle_tap(server->settings, x, y);
    return;
  }

  // Dann Control Center prüfen (liegt ganz oben)
  if (server->control_center && server->control_center->visible) {
    control_center_handle_tap(server->control_center, x, y);
    return;
  }

  // Dann Benachrichtigungs-Shade prüfen
  if (server->notify && server->notify->visible) {
    notify_handle_tap(server->notify, x, y);
    return;
  }

  // Dann Recents-Overlay prüfen
  if (server->recents && server->recents->visible) {
    recents_handle_tap(server->recents, x, y);
    return;
  }

  // Dann Launcher prüfen
  if (server->launcher && server->launcher->visible) {
    launcher_handle_tap(server->launcher, x, y);
    return;
  }

  // Home Screen Icons prüfen (wenn kein Overlay aktiv)
  if (server->homescreen) {
    homescreen_handle_tap(server->homescreen, x, y);
  }

  // Bar-Buttons prüfen (Back, Home, Recents)
  int btn = bar_check_tap(server->bar, x, y);
  if (btn >= 0) {
    switch (btn) {
    case 0: // Zurück-Button
      {
        // Fokussierte Oberfläche holen
        struct wlr_surface *focused = server->seat->keyboard_state.focused_surface;
        if (focused) {
          // Als XDG-Surface interpretieren
          struct wlr_xdg_surface *xdg =
              wlr_xdg_surface_try_from_wlr_surface(focused);
          // Nur schließen wenn es ein Top-Level-Fenster ist
          if (xdg && xdg->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
            wlr_xdg_toplevel_send_close(xdg->toplevel);
          }
        }
      }
      break;
    case 1: // Home-Button, Launcher ein/aus
      launcher_toggle(server->launcher);
      break;
    case 2: // Recents-Button
      if (server->recents->visible) {
        recents_hide(server->recents);
      } else {
        recents_show(server->recents);
      }
      break;
    case 3: // Keyboard-Button, On-Screen-Tastatur ein-/ausblenden
      if (server->osk) {
        osk_set_visible(server->osk, !server->osk->visible);
      }
      break;
    case 4: // Maus-Button, Maus-Modus ein-/ausschalten
      pointer_toggle(server);
      break;
    }
    return;
  }
}

// Finger hebt ab: Gesture auswerten, Taps an die UI weiterleiten
static void touch_up(struct wl_listener *listener, void *data) {
  // Touch-Objekt und Event-Daten holen
  struct phone_touch *touch = wl_container_of(listener, touch, up);
  struct phone_server *server = touch->server;
  struct wlr_touch_up_event *event = data;

  // Auto-Sperre: Leerlaufzeit aktualisieren
  server->last_input_time = time(NULL);

  // Slider-Zieh-Geste beenden
  server->cc_drag_slider = NULL;

  // Gesamtweg seit Gesture-Start ausrechnen
  double dx = fabs(server->current_gesture.last_x - server->current_gesture.start_x);
  double dy = fabs(server->current_gesture.last_y - server->current_gesture.start_y);

  // War es ein Tap? Kein Swipe und unter 20px Bewegung
  bool was_tap = !server->current_gesture.threshold_met &&
                 dx < 20.0 && dy < 20.0;

  // Gesture beenden, das wertet dann auch Swipe-Aktionen aus
  gesture_end(&server->current_gesture, server->current_gesture.last_x,
              server->current_gesture.last_y, event->time_msec, server);

  // Touch-Punkt immer freigeben damit der Seat sauber bleibt
  wlr_seat_touch_notify_up(server->seat, event->time_msec, event->touch_id);
  wlr_seat_touch_notify_frame(server->seat);

  // Wenn's kein Tap war, sind wir fertig
  if (!was_tap) return;

  // Position des Taps merken
  double tx = server->current_gesture.last_x;
  double ty = server->current_gesture.last_y;

  // UI-Tap-Handling (Control Center, Recents, Launcher, Home, Bar)
  handle_ui_tap(server, tx, ty, event->time_msec);
}

// Finger bewegt sich: Gesture-Update und Touch-Motion weiterleiten
static void touch_motion(struct wl_listener *listener, void *data) {
  // Touch-Objekt und Event-Daten holen
  struct phone_touch *touch = wl_container_of(listener, touch, motion);
  struct phone_server *server = touch->server;
  struct wlr_touch_motion_event *event = data;

  // Koordinaten umrechnen
  double lx, ly;
  touch_absolute_to_layout(server, event->x, event->y, &lx, &ly);

  // Gesture-Tracking updaten
  gesture_update(&server->current_gesture, lx, ly, event->time_msec);

  // Auto-Sperre: Leerlaufzeit aktualisieren
  server->last_input_time = time(NULL);

  // Control Center Slider-Zieh-Geste verarbeiten
  if (server->cc_drag_slider && server->control_center &&
      server->control_center->visible) {
    struct phone_cc_slider *s = server->cc_drag_slider;
    float frac = (float)(lx - s->x) / s->w;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    control_center_slider_set(server->control_center, s, frac);
  }

  // Welche Oberfläche ist unter dem Finger?
  struct wlr_surface *surface = NULL;
  double sx, sy;
  touch_get_surface_at(server, lx, ly, &surface, &sx, &sy);

  if (surface) {
    // Touch-Bewegung an die Oberfläche weiterleiten
    wlr_seat_touch_notify_motion(server->seat, event->time_msec,
                                 event->touch_id, sx, sy);
    // Frame-Event schicken
    wlr_seat_touch_notify_frame(server->seat);
  }
}

// Touch-Callbacks registrieren
void input_handle_touch(struct phone_touch *touch) {
  touch->destroy.notify = touch_destroy;
  touch->down.notify = touch_down;
  touch->up.notify = touch_up;
  touch->motion.notify = touch_motion;
}

// Sucht die View, deren Surface angegeben ist
static struct phone_view *find_view_for_surface(struct phone_server *server,
                                                struct wlr_surface *surface) {
  struct phone_workspace *ws;
  struct phone_view *view;
  // Durch alle Workspaces und deren Views suchen
  wl_list_for_each(ws, &server->workspaces, link) {
    wl_list_for_each(view, &ws->views, link) {
      if (view->mapped && view->xdg_surface->surface == surface) return view;
    }
  }
  return NULL; // Keine View gefunden
}

// Mauszeiger auf neue Position setzen (innerhalb des Layouts halten)
static void pointer_update_position(struct phone_server *server, double x, double y) {
  struct wlr_box box;
  wlr_output_layout_get_box(server->output_layout, NULL, &box);
  // Position auf den sichtbaren Bereich begrenzen
  server->pointer_x = x < box.x ? box.x : (x >= box.x + box.width ? box.x + box.width - 1 : x);
  server->pointer_y = y < box.y ? box.y : (y >= box.y + box.height ? box.y + box.height - 1 : y);
  // Mauszeiger-Buffer in der Szene verschieben
  if (server->cursor_buf) {
    wlr_scene_node_set_position(&server->cursor_buf->node,
        (int)server->pointer_x, (int)server->pointer_y);
  }
}

// Maus-Device zerstört: Listener entfernen und Speicher freigeben
static void pointer_destroy(struct wl_listener *listener, void *data) {
  // Pointer-Objekt das zerstört wird
  struct phone_pointer *pointer = wl_container_of(listener, pointer, destroy);
  struct phone_server *server = pointer->server;
  // Alle Listener ordentlich entfernen
  wl_list_remove(&pointer->link);
  wl_list_remove(&pointer->destroy.link);
  wl_list_remove(&pointer->motion.link);
  wl_list_remove(&pointer->motion_absolute.link);
  wl_list_remove(&pointer->button.link);
  wl_list_remove(&pointer->axis.link);
  free(pointer);
  // Seat updaten weil die Maus weg ist
  update_seat_capabilities(server);
}

// Relative Mausbewegung: Position aktualisieren und Hover weiterleiten
static void pointer_motion(struct wl_listener *listener, void *data) {
  // Pointer-Objekt und Event-Daten holen
  struct phone_pointer *pointer = wl_container_of(listener, pointer, motion);
  struct phone_server *server = pointer->server;
  struct wlr_pointer_motion_event *event = data;

  // Maus-Modus aus? Dann ignorieren
  if (!server->mouse_enabled) return;

  // Relative Bewegung auf die Mausposition anrechnen
  pointer_update_position(server, server->pointer_x + event->delta_x,
                          server->pointer_y + event->delta_y);

  // Welche Oberfläche liegt unter dem Mauszeiger?
  struct wlr_surface *surface = NULL;
  double sx, sy;
  touch_get_surface_at(server, server->pointer_x, server->pointer_y,
                       &surface, &sx, &sy);

  if (server->pointer_pressed) {
    // Maustaste gehalten: als Drag/Swipe behandeln
    gesture_update(&server->current_gesture, server->pointer_x,
                   server->pointer_y, event->time_msec);
    if (surface) {
      wlr_seat_pointer_notify_motion(server->seat, event->time_msec, sx, sy);
      wlr_seat_pointer_notify_frame(server->seat);
    }
  } else if (surface) {
    // Hover: Mauszeiger-Fokus an die Oberfläche geben
    wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
    wlr_seat_pointer_notify_motion(server->seat, event->time_msec, sx, sy);
    wlr_seat_pointer_notify_frame(server->seat);
  } else {
    // Keine Oberfläche unter dem Mauszeiger
    wlr_seat_pointer_notify_clear_focus(server->seat);
  }
}

// Absolute Mausbewegung (normiert 0..1): Position setzen und Hover weiterleiten
static void pointer_motion_absolute(struct wl_listener *listener, void *data) {
  // Pointer-Objekt und Event-Daten holen
  struct phone_pointer *pointer = wl_container_of(listener, pointer, motion_absolute);
  struct phone_server *server = pointer->server;
  struct wlr_pointer_motion_absolute_event *event = data;

  // Maus-Modus aus? Dann ignorieren
  if (!server->mouse_enabled) return;

  // Normierte (0..1) Koordinaten in Layout-Koordinaten umrechnen
  struct wlr_box box;
  wlr_output_layout_get_box(server->output_layout, NULL, &box);
  pointer_update_position(server, box.x + event->x * box.width,
                          box.y + event->y * box.height);

  // Hover-Verhalten wie bei relativer Bewegung
  struct wlr_surface *surface = NULL;
  double sx, sy;
  touch_get_surface_at(server, server->pointer_x, server->pointer_y,
                       &surface, &sx, &sy);

  if (server->pointer_pressed) {
    gesture_update(&server->current_gesture, server->pointer_x,
                   server->pointer_y, event->time_msec);
    if (surface) {
      wlr_seat_pointer_notify_motion(server->seat, event->time_msec, sx, sy);
      wlr_seat_pointer_notify_frame(server->seat);
    }
  } else if (surface) {
    wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
    wlr_seat_pointer_notify_motion(server->seat, event->time_msec, sx, sy);
    wlr_seat_pointer_notify_frame(server->seat);
  } else {
    wlr_seat_pointer_notify_clear_focus(server->seat);
  }
}

// Maustaste: Klicks, Gesten und UI-Taps verarbeiten (wie ein Finger)
static void pointer_button(struct wl_listener *listener, void *data) {
  // Pointer-Objekt und Event-Daten holen
  struct phone_pointer *pointer = wl_container_of(listener, pointer, button);
  struct phone_server *server = pointer->server;
  struct wlr_pointer_button_event *event = data;

  // Maus-Modus aus? Dann ignorieren
  if (!server->mouse_enabled) return;

  // Rechte Maustaste = Android-Zurück (aktive App schließen)
  if (event->button == BTN_RIGHT) {
    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
      struct wlr_surface *focused = server->seat->keyboard_state.focused_surface;
      if (focused) {
        struct wlr_xdg_surface *xdg =
            wlr_xdg_surface_try_from_wlr_surface(focused);
        if (xdg && xdg->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
          wlr_xdg_toplevel_send_close(xdg->toplevel);
        }
      }
    }
    return;
  }

  // Nur linke Maustaste verarbeiten (wie ein Finger)
  if (event->button != BTN_LEFT) return;

  // Welche Oberfläche liegt unter dem Mauszeiger?
  struct wlr_surface *surface = NULL;
  double sx, sy;
  touch_get_surface_at(server, server->pointer_x, server->pointer_y,
                       &surface, &sx, &sy);

  // Fokus sicherstellen (falls der Klick ohne vorherige Bewegung kommt)
  if (surface) {
    wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
  }

  // Button-Event an die Oberfläche weiterleiten
  wlr_seat_pointer_notify_button(server->seat, event->time_msec,
                                 event->button, event->state);
  wlr_seat_pointer_notify_frame(server->seat);

  if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
    // Maustaste gedrückt: Gesture starten
    server->pointer_pressed = true;
    gesture_begin(&server->current_gesture, server->pointer_x,
                  server->pointer_y, event->time_msec, -1);
    // Fenster unter dem Mauszeiger fokussieren
    if (surface) {
      struct phone_view *view = find_view_for_surface(server, surface);
      if (view) focus_view(view, surface);
    }
  } else if (server->pointer_pressed) {
    // Maustaste losgelassen: Gesture auswerten
    server->pointer_pressed = false;

    // War es ein Tap? Kein Swipe und unter 20px Bewegung
    double dx = fabs(server->pointer_x - server->current_gesture.start_x);
    double dy = fabs(server->pointer_y - server->current_gesture.start_y);
    bool was_tap = !server->current_gesture.threshold_met &&
                   dx < 20.0 && dy < 20.0;

    double tx = server->pointer_x;
    double ty = server->pointer_y;
    gesture_end(&server->current_gesture, server->pointer_x,
                server->pointer_y, event->time_msec, server);

    // Wenn's ein Tap war, UI-Klicks verarbeiten
    if (was_tap) handle_ui_tap(server, tx, ty, event->time_msec);
  }
}

// Mausrad/Scroll: Axis-Event an die Oberfläche unter dem Mauszeiger senden
static void pointer_axis(struct wl_listener *listener, void *data) {
  // Pointer-Objekt und Event-Daten holen
  struct phone_pointer *pointer = wl_container_of(listener, pointer, axis);
  struct phone_server *server = pointer->server;
  struct wlr_pointer_axis_event *event = data;

  // Maus-Modus aus? Dann ignorieren
  if (!server->mouse_enabled) return;

  // Welche Oberfläche liegt unter dem Mauszeiger?
  struct wlr_surface *surface = NULL;
  double sx, sy;
  touch_get_surface_at(server, server->pointer_x, server->pointer_y,
                       &surface, &sx, &sy);
  if (surface) {
    // Fokus setzen falls nötig, dann Scroll-Event weiterleiten
    wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
    wlr_seat_pointer_notify_axis(server->seat, event->time_msec,
        event->orientation, event->delta, event->delta_discrete,
        event->source, event->relative_direction);
    wlr_seat_pointer_notify_frame(server->seat);
  }
}

// Maus-Modus ein-/ausschalten (Ctrl+Shift+L)
void pointer_toggle(struct phone_server *server) {
  server->mouse_enabled = !server->mouse_enabled;

  if (server->mouse_enabled) {
    // Mauszeiger in die Bildschirmmitte setzen und einblenden
    struct wlr_box box;
    wlr_output_layout_get_box(server->output_layout, NULL, &box);
    server->pointer_pressed = false;
    pointer_update_position(server, box.x + box.width / 2.0,
                            box.y + box.height / 2.0);
    if (server->cursor_buf) {
      wlr_scene_node_set_enabled(&server->cursor_buf->node, true);
    }
    wlr_log(WLR_INFO, "%s", "Mouse mode ENABLED (Ctrl+Shift+L)");
  } else {
    // Mauszeiger verstecken und laufenden Drag abbrechen
    if (server->cursor_buf) {
      wlr_scene_node_set_enabled(&server->cursor_buf->node, false);
    }
    if (server->pointer_pressed) {
      server->pointer_pressed = false;
      server->current_gesture.active = false;
    }
    wlr_seat_pointer_notify_clear_focus(server->seat);
    wlr_log(WLR_INFO, "%s", "Mouse mode DISABLED (Ctrl+Shift+L)");
  }

  // Seat-Capabilities aktualisieren (Clients sehen den Pointer verschwinden/auftauchen)
  update_seat_capabilities(server);
}

// Der Mauszeiger darf Eingabe-Hit-Tests nicht blockieren: er liegt als
// Scene-Buffer ganz oben und würde sonst jede Oberfläche darunter "abdecken"
static bool pointer_cursor_no_input(struct wlr_scene_buffer *buffer,
                                    double *sx, double *sy) {
  return false; // Mauszeiger fängt keine Eingaben ab
}

// Mauszeiger-Scene-Buffer initialisieren
void pointer_init(struct phone_server *server) {
  server->pointer_pressed = false;
  server->ctrl_pressed = false;
  server->shift_pressed = false;

  // Mauszeiger-Pfeil als Pixel-Buffer rendern
  struct wlr_buffer *buf = phone_render_cursor(server->allocator);
  if (!buf) return;
  // Als Scene-Buffer in den Overlay-Layer (ganz vorne) einfügen
  server->cursor_buf = wlr_scene_buffer_create(server->layer_overlay_tree, buf);
  // Original-Buffer freigeben, wird nicht mehr gebraucht
  wlr_buffer_drop(buf);
  if (!server->cursor_buf) return;

  // Mauszeiger aus den Eingabe-Hit-Tests ausschließen
  server->cursor_buf->point_accepts_input = pointer_cursor_no_input;

  // Maus-Modus ist standardmäßig AUS. Einschalten per Ctrl+Shift+L.
  server->mouse_enabled = false;
  struct wlr_box box;
  wlr_output_layout_get_box(server->output_layout, NULL, &box);
  server->pointer_x = box.x + box.width / 2.0;
  server->pointer_y = box.y + box.height / 2.0;
  wlr_scene_node_set_position(&server->cursor_buf->node,
      (int)server->pointer_x, (int)server->pointer_y);
  wlr_scene_node_set_enabled(&server->cursor_buf->node, false);
}

// Pointer-Callbacks registrieren
void input_handle_pointer(struct phone_pointer *pointer) {
  pointer->destroy.notify = pointer_destroy;
  pointer->motion.notify = pointer_motion;
  pointer->motion_absolute.notify = pointer_motion_absolute;
  pointer->button.notify = pointer_button;
  pointer->axis.notify = pointer_axis;
}
