#define _POSIX_C_SOURCE 200809L // POSIX Features aktivieren (clock_gettime, etc.)
#include "server.h" // Unser Header mit allen Strukturen
#include "lock.h"   // Sperrbildschirm
#include "notify.h" // Benachrichtigungen
#include "osd.h"    // OSD-Overlay
#include "power.h"  // Power-Menü
#include "render.h" // Text-/Gradient-Rendering
#include "settings.h" // Einstellungen-Seite
#include "theme.h"  // Theme-Farben
#include "toast.h"  // Toast-Banner
#include <stdlib.h> // malloc, calloc, free
#include <string.h> // memset
#include <time.h> // clock_gettime für Frame-Timing
#include <wlr/render/gles2.h> // GLES2 Renderer als Fallback

// Wallpaper: Verlauf aus dem Theme rendern und als Hintergrund anzeigen
void wallpaper_update(struct phone_server *server) {
  if (!server->theme || !server->allocator) return;

  struct phone_output *output = NULL;
  if (!wl_list_empty(&server->outputs))
    output = wl_container_of(server->outputs.next, output, link);
  int sw = output ? output->wlr_output->width : 800;
  int sh = output ? output->wlr_output->height : 480;
  if (sw < 1 || sh < 1) return;

  struct wlr_buffer *buf = phone_render_gradient(server->allocator, sw, sh,
      server->theme->wallpaper_start[0], server->theme->wallpaper_start[1],
      server->theme->wallpaper_start[2],
      server->theme->wallpaper_end[0], server->theme->wallpaper_end[1],
      server->theme->wallpaper_end[2]);
  if (!buf) return;

  // Alten Wallpaper-Buffer zerstören falls vorhanden
  if (server->wallpaper_buf) {
    wlr_scene_node_destroy(&server->wallpaper_buf->node);
    server->wallpaper_buf = NULL;
  }
  server->wallpaper_wlr_buf = buf;
  server->wallpaper_buf = wlr_scene_buffer_create(
      server->layer_background_tree, buf);
  wlr_buffer_drop(buf);
  if (server->wallpaper_buf) {
    wlr_scene_node_set_position(&server->wallpaper_buf->node, 0, 0);
  }
}

// Nachtmodus-Overlay an die Bildschirmgröße anpassen
static void night_resize(struct phone_server *server) {
  if (!server->night_rect) return;
  struct phone_output *output = NULL;
  if (!wl_list_empty(&server->outputs))
    output = wl_container_of(server->outputs.next, output, link);
  int sw = output ? output->wlr_output->width : 800;
  int sh = output ? output->wlr_output->height : 480;
  wlr_scene_rect_set_size(server->night_rect, sw, sh);
}

// Nachtmodus ein-/ausschalten (aus controlcenter.c aufgerufen)
void night_set(struct phone_server *server, bool on) {
  if (!server) return;
  server->night_enabled = on;
  if (server->night_rect) {
    wlr_scene_node_set_enabled(&server->night_rect->node, on);
  }
  wlr_log(WLR_INFO, "Night mode %s", on ? "ON" : "OFF");
}

// Nachtmodus-Zustand abfragen
bool night_get(struct phone_server *server) {
  return server ? server->night_enabled : false;
}

// Auto-Sperre: Alle 5 Sekunden prüfen ob die Leerlaufzeit überschritten wurde
static int idle_lock_tick(void *data) {
  struct phone_server *server = data;
  if (!server) return 1;

  if (server->auto_lock_after > 0 && !(server->lock && server->lock->visible)) {
    time_t now = time(NULL);
    if (now - server->last_input_time >= server->auto_lock_after) {
      wlr_log(WLR_INFO, "Auto-lock: idle timeout reached (%ds), locking",
          server->auto_lock_after);
      if (server->lock) lock_show(server->lock);
    }
  }
  return 1; /* keep timer running */
}

// Neuer Output (Bildschirm) angeschlossen: Mode setzen und Scene-Output anlegen
static void new_output_notify(struct wl_listener *listener, void *data) {
  // Server aus dem Listener extrahieren (wlroots Pattern)
  struct phone_server *server = wl_container_of(listener, server, new_output);
  struct wlr_output *wlr_output = data; // Das neue Display/Output-Objekt

  // Renderer für diesen Output initialisieren (braucht Allocator + Renderer)
  wlr_output_init_render(wlr_output, server->allocator, server->renderer);

  // Output-State vorbereiten
  struct wlr_output_state state;
  wlr_output_state_init(&state);
  wlr_output_state_set_enabled(&state, true); // Output einschalten

  // Bevorzugten Display-Modus holen (Beste Auflösung + Refresh Rate)
  struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
  if (mode) {
    wlr_output_state_set_mode(&state, mode); // Bevorzugten Modus setzen
  } else {
    // Fallback: 1280x720 wenn kein Modus verfügbar ist
    wlr_output_state_set_custom_mode(&state, 1280, 720, 0);
  }

  // State anwenden (Modus wirklich setzen)
  wlr_output_commit_state(wlr_output, &state);
  wlr_output_state_finish(&state); // State aufräumen

  // Prüfen ob der Output überhaupt eine Größe hat
  if (wlr_output->width < 1 || wlr_output->height < 1) {
    wlr_log(WLR_ERROR, "output %s has no size after mode commit, skipping",
        wlr_output->name);
    return; // Skip - kein sinnvolles Display
  }

  wlr_log(WLR_INFO, "new output %s: %dx%d",
      wlr_output->name, wlr_output->width, wlr_output->height);

  // Phone_output Struktur allokieren und initialisieren
  struct phone_output *output = calloc(1, sizeof(struct phone_output));
  if (!output) return; // Kein Speicher mehr übrig
  output->server = server; // Server-Referenz setzen
  output->wlr_output = wlr_output; // wlroots Output-Objekt speichern

  // Scene-Output erstellen (damit die Scene weiß wie sie rendern soll)
  output->scene_output = wlr_scene_output_create(server->scene, wlr_output);
  if (!output->scene_output) {
    free(output); // Cleanup wenn fehlgeschlagen
    return;
  }

  // Output ins Layout einfügen (Auto-Positionierung)
  wlr_output_layout_add_auto(server->output_layout, wlr_output);

  // Destroy-Listener hängen (wenn Output entfernt wird)
  output->destroy.notify = output_begin_destroy;
  wl_signal_add(&wlr_output->events.destroy, &output->destroy);

  // Frame-Handler setuppen (wird bei jedem VSync aufgerufen)
  output_handle_frame(output);

  // Output in die Liste einfügen
  wl_list_insert(&server->outputs, &output->link);

  // Hintergrund-Rechteck an die neue Größe anpassen
  wlr_scene_rect_set_size(server->background_rect,
      wlr_output->width, wlr_output->height);
  // Wallpaper und Nachtmodus an die Bildschirmgröße anpassen
  wallpaper_update(server);
  night_resize(server);

  // Layer-Surfaces neu anordnen für diesen Output
  layer_surface_arrange(server, wlr_output);
  // Navigation Bar und Statusbar aktualisieren
  bar_update(server->bar);
  statusbar_update(server->statusbar);
  // On-Screen-Tastatur an die Bildschirmgröße anpassen
  osk_update_size(server->osk, wlr_output->width, wlr_output->height);
}

// Seat-Capabilities aktualisieren (Keyboard/Touch/Pointer an Clients melden)
void update_seat_capabilities(struct phone_server *server) {
  uint32_t caps = 0; // Start mit keinen Fähigkeiten
  // Tastatur vorhanden? Dann Keyboard-Capability setzen
  // (Auch wenn die On-Screen-Tastatur sichtbar ist, damit Clients Key-Events
  // empfangen können)
  if (!wl_list_empty(&server->keyboards) ||
      (server->osk && server->osk->visible)) caps |= WL_SEAT_CAPABILITY_KEYBOARD;
  // Touch vorhanden? Dann Touch-Capability setzen
  if (!wl_list_empty(&server->touches)) caps |= WL_SEAT_CAPABILITY_TOUCH;
  // Maus vorhanden UND Maus-Modus aktiv? Dann Pointer-Capability setzen
  if (!wl_list_empty(&server->pointers) && server->mouse_enabled)
    caps |= WL_SEAT_CAPABILITY_POINTER;
  wlr_log(WLR_DEBUG, "update_seat_capabilities: caps=%x pointers=%d mouse=%d",
      caps, !wl_list_empty(&server->pointers), server->mouse_enabled);
  // Capabilities beim Seat aktualisieren (Clients erfahren das dann)
  wlr_seat_set_capabilities(server->seat, caps);
}

// Neues Eingabegerät (Keyboard, Touch, Maus) am Server registrieren
static void new_input_notify(struct wl_listener *listener, void *data) {
  // Server aus dem Listener extrahieren
  struct phone_server *server = wl_container_of(listener, server, new_input);
  struct wlr_input_device *device = data; // Das neue Input-Device

  // Je nach Device-Typ verschiedene Behandlung
  switch (device->type) {
  case WLR_INPUT_DEVICE_KEYBOARD: {
    // Keyboard-Struktur allokieren
    struct phone_keyboard *keyboard = calloc(1, sizeof(struct phone_keyboard));
    if (!keyboard) return; // Kein Speicher
    keyboard->server = server;
    // wlroots Keyboard aus dem Input-Device holen
    keyboard->wlr_keyboard = wlr_keyboard_from_input_device(device);
    // Listener für Destroy, Key und Modifier Events registrieren
    wl_signal_add(&device->events.destroy, &keyboard->destroy);
    wl_signal_add(&keyboard->wlr_keyboard->events.key, &keyboard->key);
    wl_signal_add(&keyboard->wlr_keyboard->events.modifiers, &keyboard->modifiers);
    // In die Keyboard-Liste einfügen
    wl_list_insert(&server->keyboards, &keyboard->link);
    // Keyboard-Handler initialisieren (Keymap laden, etc.)
    input_handle_keyboard(keyboard);
    // Seat-Capabilities updaten (jetzt gibts ein Keyboard)
    update_seat_capabilities(server);
    break;
  }
  case WLR_INPUT_DEVICE_TOUCH: {
    // Touch-Struktur allokieren
    struct phone_touch *touch = calloc(1, sizeof(struct phone_touch));
    if (!touch) return; // Kein Speicher
    touch->server = server;
    // wlroots Touch aus dem Input-Device holen
    touch->wlr_touch = wlr_touch_from_input_device(device);
    // Listener für Destroy, Down, Up und Motion Events
    wl_signal_add(&device->events.destroy, &touch->destroy);
    wl_signal_add(&touch->wlr_touch->events.down, &touch->down);
    wl_signal_add(&touch->wlr_touch->events.up, &touch->up);
    wl_signal_add(&touch->wlr_touch->events.motion, &touch->motion);
    // In die Touch-Liste einfügen
    wl_list_insert(&server->touches, &touch->link);
    // Touch-Handler initialisieren
    input_handle_touch(touch);
    // Seat-Capabilities updaten (jetzt gibts Touch)
    update_seat_capabilities(server);
    break;
  }
  case WLR_INPUT_DEVICE_POINTER: {
    wlr_log(WLR_DEBUG, "new pointer input device: %s", device->name);
    // Pointer-Struktur allokieren
    struct phone_pointer *pointer = calloc(1, sizeof(struct phone_pointer));
    if (!pointer) return; // Kein Speicher
    pointer->server = server;
    // wlroots Pointer aus dem Input-Device holen
    pointer->wlr_pointer = wlr_pointer_from_input_device(device);
    // Listener für Destroy, Motion, Button und Axis Events
    wl_signal_add(&device->events.destroy, &pointer->destroy);
    wl_signal_add(&pointer->wlr_pointer->events.motion, &pointer->motion);
    wl_signal_add(&pointer->wlr_pointer->events.motion_absolute, &pointer->motion_absolute);
    wl_signal_add(&pointer->wlr_pointer->events.button, &pointer->button);
    wl_signal_add(&pointer->wlr_pointer->events.axis, &pointer->axis);
    // In die Pointer-Liste einfügen
    wl_list_insert(&server->pointers, &pointer->link);
    // Pointer-Handler initialisieren
    input_handle_pointer(pointer);
    // Seat-Capabilities updaten (jetzt gibts eine Maus)
    update_seat_capabilities(server);
    break;
  }

  default:
    break; // Andere Device-Typen ignorieren
  }
}

// Neues XDG-Top-Level-Fenster: als View registrieren
static void new_xdg_toplevel_notify(struct wl_listener *listener, void *data) {
  struct phone_server *server = wl_container_of(listener, server, new_xdg_toplevel);
  struct wlr_xdg_toplevel *toplevel = data; // Das neue XDG Toplevel Fenster

  // View aus der XDG-Surface erstellen
  view_create(server, toplevel->base);
}

// Neue Layer-Surface (z.B. Statusleiste) registrieren
static void new_layer_surface_notify(struct wl_listener *listener, void *data) {
  struct phone_server *server = wl_container_of(listener, server, new_layer_surface);
  struct wlr_layer_surface_v1 *layer_surface = data; // Die neue Layer-Surface

  // Layer-Surface erstellen und in die Liste aufnehmen
  layer_surface_create(server, layer_surface);
}

// Neue virtuelle Tastatur (von externen Tools) registrieren
static void new_virtual_keyboard_notify(struct wl_listener *listener, void *data) {
  struct phone_server *server = wl_container_of(listener, server, new_virtual_keyboard);
  struct wlr_virtual_keyboard_v1 *vkbd = data; // Die virtuelle Tastatur
  struct wlr_input_device *device = &vkbd->keyboard.base;

  // Keyboard-Struktur allokieren (genau wie bei echtem Keyboard)
  struct phone_keyboard *keyboard = calloc(1, sizeof(struct phone_keyboard));
  if (!keyboard) return;
  keyboard->server = server;
  // Achtung: hier direkt &vkbd->keyboard statt conversion
  keyboard->wlr_keyboard = &vkbd->keyboard;
  // Listener registrieren (Destroy, Key, Modifier)
  wl_signal_add(&device->events.destroy, &keyboard->destroy);
  wl_signal_add(&keyboard->wlr_keyboard->events.key, &keyboard->key);
  wl_signal_add(&keyboard->wlr_keyboard->events.modifiers, &keyboard->modifiers);
  // In die Keyboard-Liste einfügen
  wl_list_insert(&server->keyboards, &keyboard->link);
  // Keyboard-Handler initialisieren
  input_handle_keyboard(keyboard);
}

// Neue virtuelle Maus (von externen Tools) registrieren
static void new_virtual_pointer_notify(struct wl_listener *listener, void *data) {
  struct phone_server *server = wl_container_of(listener, server, new_virtual_pointer);
  struct wlr_virtual_pointer_v1_new_pointer_event *event = data;
  struct wlr_virtual_pointer_v1 *vpointer = event->new_pointer;
  struct wlr_input_device *device = &vpointer->pointer.base;

  // Pointer-Struktur allokieren (genau wie bei echter Maus)
  struct phone_pointer *pointer = calloc(1, sizeof(struct phone_pointer));
  if (!pointer) return;
  pointer->server = server;
  // Achtung: hier direkt &vpointer->pointer statt conversion
  pointer->wlr_pointer = &vpointer->pointer;
  // Listener registrieren (Destroy, Motion, Button, Axis)
  wl_signal_add(&device->events.destroy, &pointer->destroy);
  wl_signal_add(&pointer->wlr_pointer->events.motion, &pointer->motion);
  wl_signal_add(&pointer->wlr_pointer->events.motion_absolute, &pointer->motion_absolute);
  wl_signal_add(&pointer->wlr_pointer->events.button, &pointer->button);
  wl_signal_add(&pointer->wlr_pointer->events.axis, &pointer->axis);
  // In die Pointer-Liste einfügen
  wl_list_insert(&server->pointers, &pointer->link);
  // Pointer-Handler initialisieren
  input_handle_pointer(pointer);
  // Seat-Capabilities updaten (jetzt gibts eine Maus)
  update_seat_capabilities(server);
}

// Neues text_input_v3-Objekt: On-Screen-Tastatur darüber benachrichtigen
static void new_text_input_notify(struct wl_listener *listener, void *data) {
  struct phone_server *server = wl_container_of(listener, server, new_text_input);
  struct wlr_text_input_v3 *text_input = data; // Das neue Text-Input-Objekt

  // On-Screen-Tastatur über enable/disable-Events informieren
  osk_new_text_input(server, text_input);
}

// Clipboard-Selektion übernehmen (Strg+C / Strg+V)
static void request_set_selection_notify(struct wl_listener *listener, void *data) {
  struct phone_server *server = wl_container_of(listener, server, request_set_selection);
  struct wlr_seat_request_set_selection_event *event = data;
  // Clipboard-Selektion übernehmen (Strg+C / Strg+V)
  wlr_seat_set_selection(server->seat, event->source, event->serial);
}

// Primäre Selektion setzen (Mittelklick-Einfügen)
static void request_set_primary_selection_notify(struct wl_listener *listener, void *data) {
  struct phone_server *server = wl_container_of(listener, server, request_set_primary_selection);
  struct wlr_seat_request_set_primary_selection_event *event = data;
  // Primäre Selektion setzen (Mittelklick-Einfügen)
  wlr_seat_set_primary_selection(server->seat, event->source, event->serial);
}

// Ein View fokussieren: Keyboard-Focus setzen, Fenster nach oben bringen
void focus_view(struct phone_view *view, struct wlr_surface *surface) {
  struct phone_server *server = view->server;
  struct wlr_seat *seat = server->seat;
  // Aktuell fokussierte Oberfläche merken
  struct wlr_surface *prev = seat->keyboard_state.focused_surface;

  if (prev == surface) return; // Bereits fokussiert, nichts tun

  if (prev) {
    // Alte View deaktivieren (falls es eine XDG-View war)
    struct wlr_xdg_surface *prev_xdg =
        wlr_xdg_surface_try_from_wlr_surface(prev);
    if (prev_xdg && prev_xdg->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
      wlr_xdg_toplevel_set_activated(prev_xdg->toplevel, false);
    }
    // Keyboard-Focus von der alten Surface entfernen
    wlr_seat_keyboard_notify_clear_focus(seat);
  }

  if (view->scene_tree) {
    // Scene-Node nach oben bringen (über andere Fenster)
    wlr_scene_node_raise_to_top(&view->scene_tree->node);
  }

  if (surface) {
    // Keyboard holen um Keycodes an die neue Surface zu senden
    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
    if (keyboard) {
      // Keyboard-Focus auf neue Surface setzen mit Keycodes
      wlr_seat_keyboard_notify_enter(seat, surface,
                                     keyboard->keycodes,
                                     keyboard->num_keycodes,
                                     &keyboard->modifiers);
    } else {
      // Kein Keyboard vorhanden, trotzdem Focus setzen
      wlr_seat_keyboard_notify_enter(seat, surface, NULL, 0, NULL);
    }
    // Neue View als aktiv markieren
    struct wlr_xdg_surface *xdg_surface =
        wlr_xdg_surface_try_from_wlr_surface(surface);
    if (xdg_surface && xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
      wlr_xdg_toplevel_set_activated(xdg_surface->toplevel, true);
    }
  }
}

// Server initialisieren: Display, Backend, Renderer, Scene und alle UI-Module
void server_init(struct phone_server *server) {
  // Alles auf NULL/0 setzen (sauberer Start)
  memset(server, 0, sizeof(struct phone_server));

  // Wayland Display erstellen (das zentrale Objekt)
  server->display = wl_display_create();
  // Event-Loop holen (für non-blocking Event-Verarbeitung)
  server->event_loop = wl_display_get_event_loop(server->display);

  // Backend autocreaten (erkautomatisch DRM, Wayland Nested, etc.)
  server->backend = wlr_backend_autocreate(server->event_loop, NULL);
  if (!server->backend) {
    // Fallback: Headless-Backend wenn kein Hardware-Backend verfügbar
    wlr_log(WLR_ERROR, "%s", "Autocreate failed, retrying with headless");
    setenv("WLR_BACKENDS", "headless", 1);
    server->backend = wlr_backend_autocreate(server->event_loop, NULL);
    if (!server->backend) {
      wlr_log(WLR_ERROR, "%s", "Failed to create backend");
      exit(1); // Kein Backend = kein Start
    }
  }

  // Renderer erstellen (zeichnet alles auf den Screen).
  // Standard ist der Vulkan-Renderer: damit wird das Compositing und alle
  // UI-Texturen über Vulkan auf der GPU gerendert (HW-Beschleunigung).
  // Per CLI-Argument kann "gles2" oder "auto" erzwungen werden.
  const char *rname = server->renderer_name ? server->renderer_name : "vulkan";
  int drm_fd = wlr_backend_get_drm_fd(server->backend);

  server->renderer = NULL;
  if (strcmp(rname, "vulkan") == 0) {
    server->renderer = wlr_vk_renderer_create_with_drm_fd(drm_fd);
    if (!server->renderer) {
      wlr_log(WLR_ERROR, "%s",
          "Failed to create Vulkan renderer, falling back to auto");
    }
  }
  if (!server->renderer && (strcmp(rname, "vulkan") == 0 ||
        strcmp(rname, "auto") == 0)) {
    server->renderer = wlr_renderer_autocreate(server->backend);
  } else if (!server->renderer && strcmp(rname, "gles2") == 0) {
    server->renderer = wlr_gles2_renderer_create_with_drm_fd(drm_fd);
  }
  if (!server->renderer) {
    wlr_log(WLR_ERROR, "Failed to create renderer '%s'", rname);
    exit(1);
  }

  if (wlr_renderer_is_vk(server->renderer)) {
    wlr_log(WLR_INFO, "%s", "Using Vulkan renderer (GPU hardware acceleration)");
  } else {
    wlr_log(WLR_INFO, "Using non-Vulkan renderer (requested '%s')", rname);
  }

  // Allocator erstellen (verteilt Buffer fürs Rendern)
  server->allocator = wlr_allocator_autocreate(server->backend, server->renderer);
  if (!server->allocator) {
    wlr_log(WLR_ERROR, "%s", "Failed to create allocator");
    exit(1);
  }

  // Renderer mit Wayland Display verbinden
  wlr_renderer_init_wl_display(server->renderer, server->display);

  // Compositor erstellen (Version 6 des Compositor-Protokolls)
  server->compositor = wlr_compositor_create(server->display, 6, server->renderer);

  // Sub-Compositor für Kind-Oberflächen
  wlr_subcompositor_create(server->display);

  // Output-Layout erstellen (wo sind die Monitore?)
  server->output_layout = wlr_output_layout_create(server->display);

  // Scene Graph erstellen
  server->scene = wlr_scene_create();
  // Scene an Output-Layout binden
  server->scene_layout = wlr_scene_attach_output_layout(server->scene,
                                                        server->output_layout);

  // Scene-Layer in der richtigen Reihenfolge erstellen (von hinten nach vorne)
  server->layer_background_tree = wlr_scene_tree_create(&server->scene->tree); // ganz hinten
  server->layer_bottom_tree = wlr_scene_tree_create(&server->scene->tree);
  server->workspace_tree = wlr_scene_tree_create(&server->scene->tree); // Fenster-Content
  server->layer_top_tree = wlr_scene_tree_create(&server->scene->tree);
  server->layer_overlay_tree = wlr_scene_tree_create(&server->scene->tree); // ganz vorne

  // Dunkelgrauen Hintergrund erstellen
  server->background_rect = wlr_scene_rect_create(
      server->layer_background_tree, 1, 1,
      (float[4]){0.1f, 0.1f, 0.1f, 1.0f});

  // Nächstehenden Wallpaper-Scene-Buffer im Hintergrund-Layer anlegen
  // (wird bei der ersten Output-Erstellung mit dem echten Verlauf befüllt)
  server->wallpaper_wlr_buf = NULL;
  server->wallpaper_buf = NULL;

  // Workspaces initialisieren
  workspace_init(server);

  // Idle-Notifier für Power-Management
  server->idle_notifier = wlr_idle_notifier_v1_create(server->display);

  // Presentation Time für Frame-Timing
  server->presentation = wlr_presentation_create(server->display,
                                                  server->backend, 1);

  // Data Device Manager (Clipboard & Drag-and-Drop)
  server->data_device_manager = wlr_data_device_manager_create(server->display);

  // Primary Selection Manager (Mittelklick-Einfügen)
  server->primary_selection_manager =
      wlr_primary_selection_v1_device_manager_create(server->display);

  // Screencopy Manager (Screenshots aufnehmen)
  server->screencopy_manager = wlr_screencopy_manager_v1_create(server->display);

  // Foreign Toplevel Manager (externe Apps können Fenster verwalten)
  server->foreign_toplevel_manager =
      wlr_foreign_toplevel_manager_v1_create(server->display);

  // Auto-Sperre: Timer-Callback für die Leerlaufüberwachung (alle 5 Sekunden)
  // (Die genaue Verknüpfung erfolgt in server_init, nachdem das Event-Loop exists.)

  // Viewporter für Clients
  server->viewporter = wlr_viewporter_create(server->display);

  // Data Control Manager (Clipboard-Manager Zugang)
  server->data_control_manager =
      wlr_data_control_manager_v1_create(server->display);

  // Input Method Manager (Handwriting, etc.)
  server->input_method_manager =
      wlr_input_method_manager_v2_create(server->display);

  // Text Input Manager
  server->text_input_manager =
      wlr_text_input_manager_v3_create(server->display);

  // Virtual Keyboard Manager (On-Screen Keyboard)
  server->virtual_keyboard_manager =
      wlr_virtual_keyboard_manager_v1_create(server->display);

  // Virtual Pointer Manager (Test-/Remote-Maus)
  server->virtual_pointer_manager =
      wlr_virtual_pointer_manager_v1_create(server->display);

  // XDG Output Manager (Display-Geometry Info für Clients)
  wlr_xdg_output_manager_v1_create(server->display, server->output_layout);

  // XDG Shell erstellen (Standard-Fenster-Protokoll, Version 7)
  server->xdg_shell = wlr_xdg_shell_create(server->display, 7);
  // Listener für neue XDG Toplevels registrieren
  server->new_xdg_toplevel.notify = new_xdg_toplevel_notify;
  wl_signal_add(&server->xdg_shell->events.new_toplevel,
                &server->new_xdg_toplevel);

  // Layer Shell erstellen (für Panels, Overlays, Hintergründe)
  server->layer_shell = wlr_layer_shell_v1_create(server->display, 5);
  // Listener für neue Layer-Surfaces registrieren
  server->new_layer_surface.notify = new_layer_surface_notify;
  wl_signal_add(&server->layer_shell->events.new_surface,
                &server->new_layer_surface);

  // Listener für virtuelle Tastaturen registrieren
  server->new_virtual_keyboard.notify = new_virtual_keyboard_notify;
  wl_signal_add(&server->virtual_keyboard_manager->events.new_virtual_keyboard,
                &server->new_virtual_keyboard);

  // Listener für virtuelle Mäuse registrieren
  server->new_virtual_pointer.notify = new_virtual_pointer_notify;
  wl_signal_add(&server->virtual_pointer_manager->events.new_virtual_pointer,
                &server->new_virtual_pointer);

  // Listener für text_input_v3 registrieren (On-Screen-Tastatur)
  server->new_text_input.notify = new_text_input_notify;
  wl_signal_add(&server->text_input_manager->events.new_text_input,
                &server->new_text_input);

  // Seat erstellen (Eingabe-Session für Tastatur, Maus, Touch)
  server->seat = wlr_seat_create(server->display, "seat0");

  // Clipboard-Selektion Listener
  server->request_set_selection.notify = request_set_selection_notify;
  wl_signal_add(&server->seat->events.request_set_selection,
                &server->request_set_selection);

  // Primäre Selektion Listener
  server->request_set_primary_selection.notify = request_set_primary_selection_notify;
  wl_signal_add(&server->seat->events.request_set_primary_selection,
                &server->request_set_primary_selection);

  // Listen initialisieren (leere verkettete Listen)
  wl_list_init(&server->outputs);
  wl_list_init(&server->keyboards);
  wl_list_init(&server->touches);
  wl_list_init(&server->pointers);
  wl_list_init(&server->layer_surfaces);
  wl_list_init(&server->text_inputs);

  // Listener für neue Outputs (Displays)
  server->new_output.notify = new_output_notify;
  wl_signal_add(&server->backend->events.new_output, &server->new_output);

  // Listener für neue Input-Devices
  server->new_input.notify = new_input_notify;
  wl_signal_add(&server->backend->events.new_input, &server->new_input);

  // Theme zuerst laden (alle UI-Module brauchen die Farben)
  theme_init(server);

  // Nachtmodus-Overlay im Overlay-Layer anlegen (unter den anderen Overlays,
  // damit Control Center / Launcher ungetrübt bleiben)
  server->night_enabled = false;
  server->night_rect = wlr_scene_rect_create(
      server->layer_overlay_tree, 1, 1,
      (float[4]){1.0f, 0.55f, 0.25f, 0.35f});
  wlr_scene_node_set_enabled(&server->night_rect->node, false);

  // Auto-Sperre: Standardwert (kann durch settings.conf überschrieben werden)
  server->auto_lock_after = 60;
  server->last_input_time = time(NULL);

  // UI-Komponenten initialisieren
  bar_init(server); // Navigation Bar
  launcher_init(server); // App-Launcher
  homescreen_init(server); // Home Screen
  recents_init(server); // Zuletzt geöffnete Apps
  statusbar_init(server); // Statusbar oben
  control_center_init(server); // Control Center (Quick Settings)

  // Mauszeiger initialisieren (erstmal versteckt, Maus-Modus aus)
  pointer_init(server);

  // On-Screen-Tastatur initialisieren (versteckt, erscheint bei Textfeld-Fokus)
  osk_init(server);

  // Zusätzliche Overlays: Benachrichtigungen, Power-Menü, Sperrbildschirm
  notify_init(server);
  power_init(server);
  lock_init(server);

  // Toast-Banner und OSD-Overlay initialisieren
  toast_init(server);
  osd_init(server);

  // Einstellungen-Seite initialisieren
  settings_init(server);

  // Auto-Sperre: Periodischer Timer prüft die Leerlaufzeit
  struct wl_event_loop *loop = wl_display_get_event_loop(server->display);
  server->idle_lock_timer = wl_event_loop_add_timer(loop, idle_lock_tick, server);
  if (server->idle_lock_timer)
    wl_event_source_timer_update(server->idle_lock_timer, 5000);

  wlr_log(WLR_INFO, "%s", "Phone WM server initialized");
}

// Initialen Frame auf allen Outputs committen (Idle-Callback nach Backend-Start)
static void kickstart_outputs(void *data) {
  struct phone_server *server = data;
  struct phone_output *output;
  // Durch alle Outputs iterieren und initialen Frame senden
  wl_list_for_each(output, &server->outputs, link) {
    // Prüfen ob der Output eine gültige Größe hat
    if (output->wlr_output->width < 1 || output->wlr_output->height < 1) {
      wlr_log(WLR_ERROR, "output %s has no size, skipping",
          output->wlr_output->name);
      continue; // Ungültigen Output überspringen
    }

    // Initialen Frame committen (erstes Rendering)
    if (!wlr_scene_output_commit(output->scene_output, NULL)) {
      wlr_log(WLR_ERROR, "failed to commit initial frame for %s",
          output->wlr_output->name);
      continue; // Bei Fehler überspringen
    }

    // Aktuelle Zeit holen für Frame-Done-Signal
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    // Clients mitteilen dass der Frame fertig ist
    wlr_scene_output_send_frame_done(output->scene_output, &now);

    wlr_log(WLR_INFO, "initial frame committed for %s (%dx%d)",
        output->wlr_output->name,
        output->wlr_output->width,
        output->wlr_output->height);
  }

  // Willkommensbenachrichtigung nach dem ersten Frame
  notify_post(server, "phone-wm", "Willkommen",
      "System bereit. Wischen Sie fuer die App-Vorschau.");
}

// Event-Loop starten: Socket anlegen, Backend starten, blockierend laufen
void server_run(struct phone_server *server) {
  wlr_log(WLR_INFO, "%s", "Starting Phone WM...");

  // Socket erstellen (Wayland-Verbindungspunkt für Clients)
  const char *socket = wl_display_add_socket_auto(server->display);
  if (!socket) {
    wlr_log(WLR_ERROR, "%s", "Failed to create display socket");
    exit(1);
  }

  // WAYLAND_DISPLAY Env-Variable setzen (Clients finden uns so)
  setenv("WAYLAND_DISPLAY", socket, true);
  wlr_log(WLR_INFO, "Running on Wayland display: %s", socket);

  // Backend starten (Display wird aktiviert)
  if (!wlr_backend_start(server->backend)) {
    wlr_log(WLR_ERROR, "%s", "Failed to start backend");
    exit(1);
  }

  // Idle-Callback für initialen Frame-Commit einreihen
  struct wl_event_loop *loop = wl_display_get_event_loop(server->display);
  wl_event_loop_add_idle(loop, kickstart_outputs, server);

  // Event-Loop starten (blockiert bis zum Ende)
  wl_display_run(server->display);
}

// Server herunterfahren: Clients und Display aufräumen
void server_finish(struct phone_server *server) {
  // Alle Clients beenden (sauberes Herunterfahren)
  wl_display_destroy_clients(server->display);
  // Display zerstören (alles aufräumen)
  wl_display_destroy(server->display);
}
