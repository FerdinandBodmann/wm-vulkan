#ifndef PHONE_WM_RENDER_H
#define PHONE_WM_RENDER_H

#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_buffer.h>

// Text als Pixel-Buffer rendern (Cairo + Pango, zentriert)
struct wlr_buffer *phone_render_text(struct wlr_allocator *allocator,
    const char *text, int width, int height, int ptsize, float r, float g, float b);

// Vertikaler Farbverlauf als Pixel-Buffer (für Wallpaper/Hintergrund)
struct wlr_buffer *phone_render_gradient(struct wlr_allocator *allocator,
    int width, int height, float r1, float g1, float b1, float r2, float g2, float b2);

// Abgerundetes Rechteck als Pixel-Buffer (für Panels, Karten, Tasten, Toasts)
struct wlr_buffer *phone_render_rounded_rect(struct wlr_allocator *allocator,
    int width, int height, float radius,
    float r, float g, float b, float a);

// Mauszeiger (Pfeil) als Pixel-Buffer rendern
struct wlr_buffer *phone_render_cursor(struct wlr_allocator *allocator);

// On-Screen-Tastatur-Taste rendern (abgerundetes Rechteck + zentrierter Text)
struct wlr_buffer *phone_render_key(struct wlr_allocator *allocator,
    const char *label, int width, int height, int ptsize,
    float bg_r, float bg_g, float bg_b, float fg_r, float fg_g, float fg_b);

// Gezeichnetes Vektor-Icon rendern (Pfeil, Haus, Quadrat, Tastatur, Maus)
struct wlr_buffer *phone_render_icon(struct wlr_allocator *allocator,
    int kind, int width, int height, float r, float g, float b, float a);

// Glas-Panel (blur-sim, transluzent + feiner Border + Innen-Highlight)
struct wlr_buffer *phone_render_glass(struct wlr_allocator *allocator,
    int width, int height, float radius,
    float r, float g, float b, float a);

// Pillenförmiger Hintergrund (Capsule) — für Suche, OSD, Dock
struct wlr_buffer *phone_render_pill(struct wlr_allocator *allocator,
    int width, int height,
    float r, float g, float b, float a);

// Icon-Typen für phone_render_icon
enum phone_icon {
  PHONE_ICON_BACK,   // Zurück-Pfeil
  PHONE_ICON_HOME,   // Haus
  PHONE_ICON_RECENTS,// Quadrat (App-Übersicht)
  PHONE_ICON_KEYBOARD, // Tastatur
  PHONE_ICON_MOUSE,  // Maus
  PHONE_ICON_COUNT,
};

#endif
