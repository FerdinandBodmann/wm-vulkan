#ifndef PHONE_WM_SERVER_H
#define PHONE_WM_SERVER_H

#include <wayland-server-core.h> // Wayland Core Protocol, das Fundament
#include <time.h> // time_t für Auto-Sperre / Idle-Zeitstempel
#include <wlr/backend.h> // wlroots Backend - connectet zum Hardware/Display
#include <wlr/render/allocator.h> // Buffer Allocator fürs Rendern
#include <wlr/render/vulkan.h> // Vulkan Renderer - HW-beschleunigtes Rendering
#include <wlr/render/wlr_renderer.h> // Renderer - zeichnet alles auf den Screen
#include <wlr/types/wlr_compositor.h> // Compositor - managed die Oberflächen der Clients
#include <wlr/types/wlr_data_device.h> // Clipboard & Drag-and-Drop Support
#include <wlr/types/wlr_data_control_v1.h> // Datenkontroll-Protokoll, z.B. für Clipboard-Manager
#include <wlr/types/wlr_foreign_toplevel_management_v1.h> // Externe Apps können Fenster verwalten
#include <wlr/types/wlr_idle_notify_v1.h> // Idle-Benachrichtigung, z.B. für Power-Save
#include <wlr/types/wlr_input_device.h> // Generisches Input-Device Interface
#include <wlr/types/wlr_input_method_v2.h> // Input Method (Tastatur-Alternativen wie Handwriting)
#include <wlr/types/wlr_keyboard.h> // Tastatur-Support
#include <wlr/types/wlr_layer_shell_v1.h> // Layer Shell - für Panels, Overlays, Hintergründe
#include <wlr/types/wlr_output.h> // Output = Display/Monitor
#include <wlr/types/wlr_output_layout.h> // Layout für mehrere Monitore
#include <wlr/types/wlr_pointer.h> // Maus/Pointer-Support
#include <wlr/types/wlr_presentation_time.h> // Presentation Time für VSync & Timing
#include <wlr/types/wlr_primary_selection.h> // Primäre Selektion (Mittelklick-Einfügen)
#include <wlr/types/wlr_primary_selection_v1.h> // Dito, Version 1
#include <wlr/types/wlr_scene.h> // Scene Graph API - rendert alles effizient
#include <wlr/types/wlr_screencopy_v1.h> // Screencopy - Screenshots aufnehmen
#include <wlr/types/wlr_seat.h> // Seat = Eingabe-Sitzung (Tastatur, Maus, Touch)
#include <wlr/types/wlr_subcompositor.h> // Sub-Compositor für Kind-Oberflächen
#include <wlr/types/wlr_text_input_v3.h> // Text Input Protocol v3
#include <wlr/types/wlr_touch.h> // Touch-Input Support
#include <wlr/types/wlr_viewporter.h> // Viewporter - Clients können Viewportion setzen
#include <wlr/types/wlr_virtual_keyboard_v1.h> // Virtuelle Tastatur (z.B. On-Screen)
#include <wlr/types/wlr_virtual_pointer_v1.h> // Virtuelle Maus (Test/Remote-Eingabe)
#include <wlr/types/wlr_xdg_output_v1.h> // XDG Output - Info über Display-Geometry
#include <wlr/types/wlr_xdg_shell.h> // XDG Shell - Standard-Fenster-Protokoll
#include <wlr/util/log.h> // Logging-Funktionen
#include <xkbcommon/xkbcommon.h> // XKB - Keyboard Layout & Keymap Handling
#include "launcher.h" // Unser eigener Launcher (App-Symbolleiste)
#include "recents.h" // Zuletzt geöffnete Apps
#include "statusbar.h" // Statusbar (Clock, Battery, etc.)
#include "controlcenter.h" // Control Center (Quick Settings)
#include "osk.h" // On-Screen-Tastatur
#define NUM_WORKSPACES 5 // Anzahl der Workspaces, 5 Stück

enum gesture_type {
  GESTURE_NONE, // Kein Gesture aktiv
  GESTURE_SWIPE_HORIZONTAL, // Horizontaler Swipe (Links/Rechts)
  GESTURE_SWIPE_VERTICAL, // Vertikaler Swipe (Hoch/Runter)
};

struct phone_server; // Forward Declarations
struct phone_output;
struct phone_workspace;
struct phone_view;
struct phone_layer_surface;
struct phone_bar;
struct phone_launcher;
struct phone_homescreen;
struct phone_power;
struct phone_notify;
struct phone_lock;
struct phone_toast;
struct phone_osd;
struct phone_settings;

struct phone_gesture {
  bool active; // Ob gerade ein Gesture läuft
  double start_x, start_y; // Startposition des Fingers
  double last_x, last_y; // Letzte bekannte Position
  uint32_t start_time; // Zeitstempel wann das Gesture gestartet ist
  int32_t touch_id; // Welche Finger-ID das Gesture auslöst
  enum gesture_type type; // Welche Art von Gesture (horizontal/vertikal)
  bool threshold_met; // Ob der Mindest-Schwellenwert erreicht wurde
};

struct phone_keyboard {
  struct wl_list link; // Verkettung in die Keyboard-Liste
  struct phone_server *server; // Zeiger auf den Server
  struct wlr_keyboard *wlr_keyboard; // Das wlroots Keyboard-Objekt
  struct wl_listener destroy; // Listener für "Keyboard zerstört"
  struct wl_listener key; // Listener für Tastendrücke
  struct wl_listener modifiers; // Listener für Modifier-Keys (Shift, Ctrl, etc.)
};

struct phone_touch {
  struct wl_list link; // Verkettung in die Touch-Liste
  struct phone_server *server; // Zeiger auf den Server
  struct wlr_touch *wlr_touch; // Das wlroots Touch-Objekt
  struct wl_listener destroy; // Listener für "Touch-Device zerstört"
  struct wl_listener down; // Listener für Touch-Down Event
  struct wl_listener up; // Listener für Touch-Up Event
  struct wl_listener motion; // Listener für Touch-Bewegung
};

struct phone_pointer {
  struct wl_list link; // Verkettung in die Pointer-Liste
  struct phone_server *server; // Zeiger auf den Server
  struct wlr_pointer *wlr_pointer; // Das wlroots Pointer-Objekt
  struct wl_listener destroy; // Listener für "Pointer-Device zerstört"
  struct wl_listener motion; // Listener für Mausbewegung
  struct wl_listener motion_absolute; // Listener für absolute Mausbewegung
  struct wl_listener button; // Listener für Maustasten
  struct wl_listener axis; // Listener für Mausrad/Scroll
};

// Ein text_input_v3-Objekt eines Clients (für On-Screen-Tastatur)
struct phone_text_input {
  struct wl_list link; // Verkettung in die Text-Input-Liste
  struct phone_server *server; // Zeiger auf den Server
  struct wlr_text_input_v3 *text_input; // Das wlroots Text-Input-Objekt
  struct wl_listener enable; // Listener wenn Textfeld fokussiert wird
  struct wl_listener disable; // Listener wenn Textfeld verlassen wird
  struct wl_listener destroy; // Listener wenn das Objekt zerstört wird
};

struct phone_workspace {
  struct wl_list link; // Verkettung in die Workspace-Liste
  struct phone_server *server; // Zeiger auf den Server
  int id; // Workspace-Nummer (0-4)
  struct wl_list views; // Liste aller Views auf diesem Workspace
  struct wlr_scene_tree *scene_tree; // Scene-Baum für diesen Workspace
};

struct phone_view {
  struct wl_list link; // Verkettung in die View-Liste
  struct phone_workspace *workspace; // Welchem Workspace diese View gehört
  struct phone_server *server; // Zeiger auf den Server
  struct wlr_xdg_surface *xdg_surface; // Die XDG-Oberfläche des Clients
  struct wlr_scene_tree *scene_tree; // Scene-Baum für diese View
  struct wl_listener map; // Listener wenn View sichtbar wird
  struct wl_listener unmap; // Listener wenn View unsichtbar wird
  struct wl_listener destroy; // Listener wenn View zerstört wird
  struct wl_listener commit; // Listener für Surface-Updates
  struct wl_listener request_fullscreen; // Listener für Fullscreen-Anfrage
  struct wl_listener toplevel_destroy; // Listener für Toplevel-Destruktor
  bool mapped; // Ob die View gerade angezeigt wird
  bool fullscreen; // Ob die View im Vollbildmodus ist
  bool initial_configure_pending; // Ob das erste Configure noch aussteht
};

struct phone_output {
  struct wl_list link; // Verkettung in die Output-Liste
  struct phone_server *server; // Zeiger auf den Server
  struct wlr_output *wlr_output; // Das wlroots Output-Objekt
  struct wlr_scene_output *scene_output; // Scene-Output für Rendering
  struct wl_listener frame; // Listener für Frame-Events (VSync)
  struct wl_listener destroy; // Listener für Output-Zerstörung
  struct wl_listener request_state; // Listener für State-Änderungsanfragen
  struct wl_listener commit; // Listener für Output-Commit
};

struct phone_layer_surface {
  struct wl_list link; // Verkettung in die Layer-Surface-Liste
  struct phone_server *server; // Zeiger auf den Server
  struct wlr_layer_surface_v1 *wlr_layer_surface; // Die Layer-Surface von wlroots
  struct wlr_scene_layer_surface_v1 *scene_layer; // Scene-Layer für Rendering
  struct wl_listener map; // Listener wenn Layer sichtbar wird
  struct wl_listener unmap; // Listener wenn Layer unsichtbar wird
  struct wl_listener destroy; // Listener wenn Layer zerstört wird
  struct wl_listener commit; // Listener für Surface-Updates
  bool mapped; // Ob die Layer-Surface angezeigt wird
  bool initial_configured; // Ob das erste Configure erledigt ist
};

struct phone_server {
  struct wl_display *display; // Wayland Display = das Herzstück der ganzen Sache
  struct wl_event_loop *event_loop; // Event-Loop für non-blocking I/O
  struct wlr_backend *backend; // Backend (DRM, Wayland Nested, Headless, etc.)
  struct wlr_renderer *renderer; // Renderer zum Zeichnen
  struct wlr_allocator *allocator; // Buffer-Allocator für Render-Buffers
  const char *renderer_name; // Gewählter Renderer ("vulkan", "gles2", "auto")
  struct wlr_compositor *compositor; // Unser Compositor
  struct wlr_scene *scene; // Scene Graph - das Wurzel-Element
  struct wlr_scene_output_layout *scene_layout; // Scene-Layout für Outputs
  struct wlr_output_layout *output_layout; // Output-Layout (wo sind die Monitore?)
  struct wlr_presentation *presentation; // Presentation Time für Frame-Timing
  struct wlr_idle_notifier_v1 *idle_notifier; // Idle-Notifier für Power-Management

  struct wlr_seat *seat; // Seat = Eingabe-Session (Tastatur, Maus, Touch)
  struct wlr_data_device_manager *data_device_manager; // Clipboard & DnD Manager
  struct wlr_xdg_shell *xdg_shell; // XDG Shell für normale Fenster
  struct wlr_layer_shell_v1 *layer_shell; // Layer Shell für Panels & Overlays
  struct wlr_foreign_toplevel_manager_v1 *foreign_toplevel_manager; // Externe Fenster-Verwaltung
  struct wlr_screencopy_manager_v1 *screencopy_manager; // Screenshots
  struct wlr_input_method_manager_v2 *input_method_manager; // Input Methods
  struct wlr_text_input_manager_v3 *text_input_manager; // Text Input
  struct wlr_virtual_keyboard_manager_v1 *virtual_keyboard_manager; // Virtuelle Tastaturen
  struct wlr_virtual_pointer_manager_v1 *virtual_pointer_manager; // Virtuelle Mäuse
  struct wlr_data_control_manager_v1 *data_control_manager; // Clipboard-Manager Zugang
  struct wlr_viewporter *viewporter; // Viewporter für Clients
  struct wlr_primary_selection_v1_device_manager *primary_selection_manager; // Mittelklick-Clipboard

  struct wl_list outputs; // Liste aller angeschlossenen Displays
  struct wl_list keyboards; // Liste aller Keyboards
  struct wl_list touches; // Liste aller Touch-Devices
  struct wl_list pointers; // Liste aller Maus/Pointer-Devices
  struct wl_list workspaces; // Liste aller Workspaces
  struct wl_list layer_surfaces; // Liste aller Layer-Surfaces
  struct wl_list text_inputs; // Liste aller text_input_v3-Objekte

  struct wlr_scene_tree *layer_background_tree; // Scene-Layer für Hintergründe
  struct wlr_scene_tree *layer_bottom_tree; // Scene-Layer für untere Layers
  struct wlr_scene_tree *workspace_tree; // Scene-Layer für Workspace-Inhalte
  struct wlr_scene_tree *layer_top_tree; // Scene-Layer für obere Layers
  struct wlr_scene_tree *layer_overlay_tree; // Scene-Layer für Overlays

  struct wlr_scene_rect *background_rect; // Hintergrund-Rechteck (dunkelgrau)
  struct phone_theme *theme;      // Zentrales Theme (Farben für alle UI-Module)
  struct wlr_scene_buffer *wallpaper_buf;  // Wallpaper-Verlauf (Scene-Buffer)
  struct wlr_buffer *wallpaper_wlr_buf;    // Original-WLR-Buffer des Wallpapers
  struct phone_bar *bar; // Die Android-Style Navigationsleiste unten
  struct phone_statusbar *statusbar; // Statusbar oben (Clock, Battery, etc.)
  struct phone_launcher *launcher; // App-Launcher / App-Symbolleiste
  struct phone_homescreen *homescreen; // Home Screen mit App-Icons
  struct phone_recents *recents; // Zuletzt geöffnete Apps-Übersicht
  struct phone_control_center *control_center; // Quick Settings / Control Center
  struct phone_osk *osk; // On-Screen-Tastatur
  struct phone_notify *notify; // Benachrichtigungen (Shade)
  struct phone_power *power;   // Power-Menü (Ausschalten/Neustart/Suspend)
  struct phone_lock *lock;     // Sperrbildschirm
  struct phone_toast *toast;   // Kurze Benachrichtigungs-Banner oben
  struct phone_osd *osd;       // OSD für Lautstärke/Helligkeit
  struct phone_settings *settings; // Einstellungen-Seite (Theme, Apps, Auto-Sperre)

  // Auto-Sperre: nach Ablauf der Leerlaufzeit wird gesperrt
  int auto_lock_after;               // Sekunden, 0 = Aus
  time_t last_input_time;            // Zeitstempel der letzten Eingabe
  struct wl_event_source *idle_lock_timer; // Periodischer Timer für die Auto-Sperre

  // Stummschaltung & "Nicht stören"
  bool silent;             // Lautlos (kein Audio)
  bool dnd_enabled;        // Nicht stören (keine Toast-Banner)

  // Zieh-Geste im Control Center (Slider), wird von input.c gesetzt
  struct phone_cc_slider *cc_drag_slider;

  // Nachtmodus: warmfarbenes Overlay über allen Workspace-Inhalten
  bool night_enabled;                 // Ob der Nachtmodus aktiv ist
  struct wlr_scene_rect *night_rect;  // Warmfarbiges Vollbild-Rechteck

  struct phone_gesture current_gesture; // Das gerade aktive Gesture
  int current_workspace; // Welcher Workspace gerade aktiv ist

  // Maus-Modus: ausgeschaltet bis Ctrl+Shift+L gedrückt wird
  bool mouse_enabled; // Ob der Maus-Modus aktiv ist
  bool pointer_pressed; // Ob die linke Maustaste gerade gehalten wird
  bool ctrl_pressed; // Ob Strg gerade gehalten wird (für den Toggle-Shortcut)
  bool shift_pressed; // Ob Shift gerade gehalten wird (für den Toggle-Shortcut)
  double pointer_x, pointer_y; // Aktuelle Mausposition in Layout-Koordinaten
  struct wlr_scene_buffer *cursor_buf; // Scene-Buffer für den Mauszeiger

  struct wl_listener new_output; // Listener für neue Displays
  struct wl_listener new_input; // Listener für neue Input-Devices
  struct wl_listener new_xdg_toplevel; // Listener für neue XDG-Fenster
  struct wl_listener new_layer_surface; // Listener für neue Layer-Surfaces
  struct wl_listener new_virtual_keyboard; // Listener für virtuelle Tastaturen
  struct wl_listener new_virtual_pointer; // Listener für virtuelle Mäuse
  struct wl_listener new_text_input; // Listener für neue text_input_v3-Objekte
  struct wl_listener request_set_selection; // Listener für Clipboard-Änderungen
  struct wl_listener request_set_primary_selection; // Listener für Primär-Selektion
};

struct phone_bar {
  struct phone_server *server; // Zeiger auf den Server
  struct wlr_scene_rect *bg_rect; // Hintergrund-Rechteck der Navigation Bar
  struct wlr_buffer *btn_back_label; // Buffer für den Zurück-Button Text/Icon
  struct wlr_buffer *btn_home_label; // Buffer für den Home-Button Text/Icon
  struct wlr_buffer *btn_recents_label; // Buffer für den Recents-Button Text/Icon
  struct wlr_buffer *btn_keyboard_label; // Buffer für den Keyboard-Button Text/Icon
  struct wlr_buffer *btn_mouse_label; // Buffer für den Maus-Button Text/Icon
  struct wlr_scene_buffer *btn_back_buf; // Scene-Buffer für den Zurück-Button
  struct wlr_scene_buffer *btn_home_buf; // Scene-Buffer für den Home-Button
  struct wlr_scene_buffer *btn_recents_buf; // Scene-Buffer für den Recents-Button
  struct wlr_scene_buffer *btn_keyboard_buf; // Scene-Buffer für den Keyboard-Button
  struct wlr_scene_buffer *btn_mouse_buf; // Scene-Buffer für den Maus-Button
  int btn_back_x, btn_back_y, btn_back_w, btn_back_h; // Position & Größe des Zurück-Buttons
  int btn_home_x, btn_home_y, btn_home_w, btn_home_h; // Position & Größe des Home-Buttons
  int btn_recents_x, btn_recents_y, btn_recents_w, btn_recents_h; // Position & Größe des Recents-Buttons
  int btn_keyboard_x, btn_keyboard_y, btn_keyboard_w, btn_keyboard_h; // Position & Größe des Keyboard-Buttons
  int btn_mouse_x, btn_mouse_y, btn_mouse_w, btn_mouse_h; // Position & Größe des Maus-Buttons
  int height; // Höhe der ganzen Navigation Bar
};


// Server-Funktionen
void server_init(struct phone_server *server); // Server initialisieren (alles setuppen)
void server_run(struct phone_server *server); // Server starten und Event-Loop laufen lassen
void server_finish(struct phone_server *server); // Server aufräumen und beenden
void focus_view(struct phone_view *view, struct wlr_surface *surface); // Eine View fokussieren

// Output-Funktionen
void output_begin_destroy(struct wl_listener *listener, void *data); // Output-Zerstörung starten
void output_configure(struct phone_output *output); // Output konfigurieren
void output_handle_frame(struct phone_output *output); // Frame-Event verarbeiten

// Wallpaper-Funktionen
void wallpaper_update(struct phone_server *server); // Wallpaper-Verlauf neu rendern

// Nachtmodus ein-/ausschalten (aus dem Control Center)
void night_set(struct phone_server *server, bool on);
bool night_get(struct phone_server *server);

// Input-Funktionen
void input_handle_keyboard(struct phone_keyboard *keyboard); // Keyboard-Input verarbeiten
void input_handle_touch(struct phone_touch *touch); // Touch-Input verarbeiten
void input_handle_pointer(struct phone_pointer *pointer); // Maus-Input verarbeiten
void pointer_init(struct phone_server *server); // Mauszeiger-Scene-Buffer initialisieren
void pointer_toggle(struct phone_server *server); // Maus-Modus ein-/ausschalten
void update_seat_capabilities(struct phone_server *server); // Seat-Capabilities aktualisieren

// Bar-Funktionen
void bar_init(struct phone_server *server); // Navigation Bar initialisieren
void bar_update(struct phone_bar *bar); // Navigation Bar aktualisieren
int bar_check_tap(struct phone_bar *bar, double x, double y); // Button-Tap prüfen

// Gesture-Funktionen
void gesture_begin(struct phone_gesture *gesture, double x, double y,
                   uint32_t time, int32_t touch_id); // Neues Gesture starten
void gesture_update(struct phone_gesture *gesture, double x, double y,
                    uint32_t time); // Gesture aktualisieren (Finger bewegt sich)
void gesture_end(struct phone_gesture *gesture, double x, double y,
                 uint32_t time, struct phone_server *server); // Gesture beenden und auswerten

// Workspace-Funktionen
void workspace_init(struct phone_server *server); // Workspaces initialisieren
void workspace_switch_to(struct phone_server *server, int id); // Zu anderem Workspace wechseln
struct phone_workspace *workspace_current(struct phone_server *server); // Aktuellen Workspace holen

// View-Funktionen
void view_create(struct phone_server *server,
                 struct wlr_xdg_surface *xdg_surface); // Neue View aus XDG-Surface erstellen
void view_set_fullscreen(struct phone_view *view, bool fullscreen); // View in Vollbild setzen

// Layer-Surface-Funktionen
void layer_surface_create(struct phone_server *server,
                          struct wlr_layer_surface_v1 *layer_surface); // Neue Layer-Surface erstellen
void layer_surface_arrange(struct phone_server *server,
                           struct wlr_output *output); // Layer-Surfaces anordnen

// Launcher-Funktionen
void launcher_init(struct phone_server *server); // Launcher initialisieren
void launcher_show(struct phone_launcher *launcher); // Launcher anzeigen
void launcher_hide(struct phone_launcher *launcher); // Launcher verstecken
void launcher_toggle(struct phone_launcher *launcher); // Launcher ein-/ausschalten
void launcher_search_key(struct phone_launcher *launcher,
                         uint32_t keycode); // Tastendruck für die App-Suche

// Benachrichtigungs-Funktionen
void notify_post(struct phone_server *server,
                 const char *app, const char *title, const char *body);
int notify_count(struct phone_notify *n);

// Power-/Lock-Funktionen
void power_show(struct phone_power *power);
void lock_show(struct phone_lock *lock);

// Toast-/OSD-Funktionen (kurze Banner & Overlays für Lautstärke/Helligkeit)
void toast_show(struct phone_toast *toast, const char *app, const char *title);
void osd_show(struct phone_server *server, const char *label, int pct);

// Screenshot-Funktion (grim im Hintergrund, dann Benachrichtigung)
void phone_screenshot(struct phone_server *server);

// Theme-UI live aktualisieren (Wallpaper, Bars, Statusbar neu einfärben)
void theme_refresh_ui(struct phone_server *server);

#endif
