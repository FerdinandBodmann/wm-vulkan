#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include "render.h"
#include <cairo.h>
#include <drm_fourcc.h>
#include <math.h>
#include <pango/pangocairo.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/util/log.h>

// Eigener Pixel-Buffer basierend auf wlr_buffer
struct pixel_buf {
  struct wlr_buffer base;
  unsigned char *data;
  int stride;
};

// Pixel-Buffer zerstören und Speicher freigeben
static void pixel_buf_destroy(struct wlr_buffer *wlr_buf) {
  struct pixel_buf *buf = wl_container_of(wlr_buf, buf, base);
  free(buf->data);
  free(buf);
}

// DMA-Buf wird nicht unterstützt, immer false
static bool pixel_buf_get_dmabuf(struct wlr_buffer *buffer,
    struct wlr_dmabuf_attributes *attribs) {
  return false;
}

// Shared Memory wird nicht unterstützt, immer false
static bool pixel_buf_get_shm(struct wlr_buffer *buffer,
    struct wlr_shm_attributes *attribs) {
  return false;
}

// Direkten Zugriff auf die Pixeldaten ermöglichen (für Cairo-Rendering)
static bool pixel_buf_begin_data_ptr_access(struct wlr_buffer *wlr_buf,
    uint32_t flags, void **data, uint32_t *format, size_t *stride) {
  struct pixel_buf *buf = wl_container_of(wlr_buf, buf, base);
  *data = buf->data;
  // ARGB8888 Format für Cairo verwenden
  *format = DRM_FORMAT_ARGB8888;
  *stride = buf->stride;
  return true;
}

// Zugriff beenden (hier nichts zu tun)
static void pixel_buf_end_data_ptr_access(struct wlr_buffer *buffer) {
}

// Implementierungstabelle für den wlr_buffer
static const struct wlr_buffer_impl pixel_buf_impl = {
  .destroy = pixel_buf_destroy,
  .get_dmabuf = pixel_buf_get_dmabuf,
  .get_shm = pixel_buf_get_shm,
  .begin_data_ptr_access = pixel_buf_begin_data_ptr_access,
  .end_data_ptr_access = pixel_buf_end_data_ptr_access,
};

// Text als Pixel-Buffer rendern (Cairo + Pango) — gewichtet, geglättet
struct wlr_buffer *phone_render_text(struct wlr_allocator *allocator,
    const char *text, int width, int height, int ptsize,
    float r, float g, float b) {
  (void)allocator;

  struct pixel_buf *buf = calloc(1, sizeof(struct pixel_buf));
  if (!buf) return NULL;
  buf->stride = width * 4;
  buf->data = calloc(1, buf->stride * height);
  if (!buf->data) { free(buf); return NULL; }
  wlr_buffer_init(&buf->base, &pixel_buf_impl, width, height);
  cairo_surface_t *cairo_surf = cairo_image_surface_create_for_data(
      buf->data, CAIRO_FORMAT_ARGB32, width, height, buf->stride);
  cairo_t *cr = cairo_create(cairo_surf);
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgba(cr, 0, 0, 0, 0);
  cairo_paint(cr);

  PangoLayout *layout = pango_cairo_create_layout(cr);
  PangoFontDescription *font_desc = pango_font_description_new();
  pango_font_description_set_family(font_desc, "Inter, SF Pro Display, sans-serif");
  pango_font_description_set_absolute_size(font_desc, ptsize * PANGO_SCALE);
  // heavier weight for larger headings to mimic phone typography
  if (ptsize >= 24) pango_font_description_set_weight(font_desc, PANGO_WEIGHT_LIGHT);
  else if (ptsize >= 16) pango_font_description_set_weight(font_desc, PANGO_WEIGHT_MEDIUM);
  else pango_font_description_set_weight(font_desc, PANGO_WEIGHT_NORMAL);
  pango_layout_set_font_description(layout, font_desc);
  pango_font_description_free(font_desc);
  pango_layout_set_text(layout, text, -1);
  pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
  pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);

  PangoRectangle ink;
  pango_layout_get_pixel_extents(layout, &ink, NULL);
  int text_w = ink.width;

  // subtle text shadow for readability on wallpapers
  cairo_set_source_rgba(cr, 0, 0, 0, 0.22);
  cairo_move_to(cr, (width - text_w) / 2.0 + 0.5, (height - 4) / 2.0 - ink.y + 0.7);
  pango_cairo_show_layout(cr, layout);
  cairo_set_source_rgba(cr, r, g, b, 1.0);
  cairo_move_to(cr, (width - text_w) / 2.0, (height - 4) / 2.0 - ink.y);
  pango_cairo_show_layout(cr, layout);

  g_object_unref(layout);
  cairo_destroy(cr);
  cairo_surface_destroy(cairo_surf);
  return &buf->base;
}

// Vertikaler Farbverlauf als Pixel-Buffer — 3-stop mesh + vignette (echtes Phone-Wallpaper)
struct wlr_buffer *phone_render_gradient(struct wlr_allocator *allocator,
    int width, int height, float r1, float g1, float b1, float r2, float g2, float b2) {
  (void)allocator;
  struct pixel_buf *buf = calloc(1, sizeof(struct pixel_buf));
  if (!buf) return NULL;
  buf->stride = width * 4;
  buf->data = calloc(1, buf->stride * height);
  if (!buf->data) { free(buf); return NULL; }
  wlr_buffer_init(&buf->base, &pixel_buf_impl, width, height);
  cairo_surface_t *cairo_surf = cairo_image_surface_create_for_data(
      buf->data, CAIRO_FORMAT_ARGB32, width, height, buf->stride);
  cairo_t *cr = cairo_create(cairo_surf);

  // Base vertical gradient with intermediate tint for depth
  {
    cairo_pattern_t *pat = cairo_pattern_create_linear(0, 0, 0, height);
    // midpoint tint: warm mix for dark theme, cool lift for light
    float rm = r1 * 0.55f + r2 * 0.45f + 0.06f;
    float gm = g1 * 0.55f + g2 * 0.45f + 0.04f;
    float bm = b1 * 0.35f + b2 * 0.65f + 0.08f;
    if (rm > 1) rm = 1; if (gm > 1) gm = 1; if (bm > 1) bm = 1;
    cairo_pattern_add_color_stop_rgb(pat, 0.0, r1, g1, b1);
    cairo_pattern_add_color_stop_rgb(pat, 0.45, rm, gm, bm);
    cairo_pattern_add_color_stop_rgb(pat, 1.0, r2, g2, b2);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source(cr, pat);
    cairo_paint(cr);
    cairo_pattern_destroy(pat);
  }
  // Diagonal accent wash — top-right magenta/indigo lift
  {
    cairo_pattern_t *pat = cairo_pattern_create_linear(width * 0.4, 0, width, height * 0.65);
    cairo_pattern_add_color_stop_rgba(pat, 0.0, 0.55, 0.35, 0.95, 0.0);
    cairo_pattern_add_color_stop_rgba(pat, 0.5, 0.55, 0.35, 0.95, 0.10);
    cairo_pattern_add_color_stop_rgba(pat, 1.0, 0.10, 0.55, 0.95, 0.14);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_set_source(cr, pat);
    cairo_paint(cr);
    cairo_pattern_destroy(pat);
  }
  // Top glow — soft radial highlight as on iOS wallpapers
  {
    cairo_pattern_t *pat = cairo_pattern_create_radial(width * 0.52, height * 0.18, 0,
                                                       width * 0.52, height * 0.18, width * 0.9);
    cairo_pattern_add_color_stop_rgba(pat, 0.0, 1.0, 1.0, 1.0, 0.09);
    cairo_pattern_add_color_stop_rgba(pat, 0.55, 1.0, 1.0, 1.0, 0.03);
    cairo_pattern_add_color_stop_rgba(pat, 1.0, 1.0, 1.0, 1.0, 0.0);
    cairo_set_source(cr, pat);
    cairo_paint(cr);
    cairo_pattern_destroy(pat);
  }
  // Subtle vignette to frame the screen like a real phone
  {
    cairo_pattern_t *pat = cairo_pattern_create_radial(width/2.0, height/2.0, 0,
                                                       width/2.0, height/2.0, hypot(width, height)/1.1);
    cairo_pattern_add_color_stop_rgba(pat, 0.72, 0, 0, 0, 0.0);
    cairo_pattern_add_color_stop_rgba(pat, 1.0, 0, 0, 0, 0.28);
    cairo_set_source(cr, pat);
    cairo_paint(cr);
    cairo_pattern_destroy(pat);
  }

  cairo_destroy(cr);
  cairo_surface_destroy(cairo_surf);
  return &buf->base;
}

// Mauszeiger als Pixel-Buffer rendern (klassischer Pfeil, Spitze oben links)
struct wlr_buffer *phone_render_cursor(struct wlr_allocator *allocator) {
  (void)allocator;

  const int w = 24;
  const int h = 24;

  // Speicher für den Pixel-Buffer allozieren
  struct pixel_buf *buf = calloc(1, sizeof(struct pixel_buf));
  if (!buf) return NULL;

  // Stride = Breite * 4 Bytes (ARGB)
  buf->stride = w * 4;
  // Pixeldaten-Puffer allocieren
  buf->data = calloc(1, buf->stride * h);
  if (!buf->data) {
    free(buf);
    return NULL;
  }

  // wlr_buffer initialisieren mit unseren Callbacks
  wlr_buffer_init(&buf->base, &pixel_buf_impl, w, h);

  // Cairo-Oberfläche auf unseren Pixel-Puffer legen
  cairo_surface_t *cairo_surf = cairo_image_surface_create_for_data(
      buf->data, CAIRO_FORMAT_ARGB32, w, h, buf->stride);
  cairo_t *cr = cairo_create(cairo_surf);

  // Hintergrund komplett transparent machen
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgba(cr, 0, 0, 0, 0);
  cairo_paint(cr);

  // Weißer Pfeil mit dunkler Kontur (damit er auf jedem Hintergrund sichtbar ist)
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  cairo_move_to(cr, 0, 0);
  cairo_line_to(cr, 0, 18);
  cairo_line_to(cr, 4, 13);
  cairo_line_to(cr, 8, 19);
  cairo_line_to(cr, 10, 17);
  cairo_line_to(cr, 6, 11);
  cairo_line_to(cr, 13, 10);
  cairo_close_path(cr);
  // Erst schwarze Kontur als dickere Umrisslinie
  cairo_set_source_rgba(cr, 0, 0, 0, 0.8);
  cairo_set_line_width(cr, 2.0);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
  cairo_stroke_preserve(cr);
  // Dann weiß ausfüllen
  cairo_set_source_rgba(cr, 1, 1, 1, 1.0);
  cairo_fill(cr);

  cairo_destroy(cr);
  cairo_surface_destroy(cairo_surf);

  // Fertigen Buffer zurückgeben
  return &buf->base;
}

// Gezeichnetes Vektor-Icon rendern — gefüllt, dickere Striche wie iOS SF Symbols
struct wlr_buffer *phone_render_icon(struct wlr_allocator *allocator,
    int kind, int width, int height, float r, float g, float b, float a) {
  (void)allocator;
  struct pixel_buf *buf = calloc(1, sizeof(struct pixel_buf));
  if (!buf) return NULL;
  buf->stride = width * 4;
  buf->data = calloc(1, buf->stride * height);
  if (!buf->data) { free(buf); return NULL; }
  wlr_buffer_init(&buf->base, &pixel_buf_impl, width, height);
  cairo_surface_t *cairo_surf = cairo_image_surface_create_for_data(
      buf->data, CAIRO_FORMAT_ARGB32, width, height, buf->stride);
  cairo_t *cr = cairo_create(cairo_surf);
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgba(cr, 0, 0, 0, 0);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  cairo_set_source_rgba(cr, r, g, b, a);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  double cx = width / 2.0, cy = height / 2.0;
  switch (kind) {
  case PHONE_ICON_BACK:
    cairo_set_line_width(cr, 2.6);
    cairo_move_to(cr, cx + 6, cy - 6.5);
    cairo_line_to(cr, cx - 4.5, cy);
    cairo_line_to(cr, cx + 6, cy + 6.5);
    cairo_stroke(cr);
    break;
  case PHONE_ICON_HOME: {
    // filled house with cutout door
    cairo_move_to(cr, cx, cy - 8.5);
    cairo_line_to(cr, cx + 8.5, cy - 0.5);
    cairo_line_to(cr, cx + 7.2, cy - 0.5);
    cairo_line_to(cr, cx + 7.2, cy + 7.5);
    cairo_line_to(cr, cx + 1.8, cy + 7.5);
    cairo_line_to(cr, cx + 1.8, cy + 2.5);
    cairo_line_to(cr, cx - 1.8, cy + 2.5);
    cairo_line_to(cr, cx - 1.8, cy + 7.5);
    cairo_line_to(cr, cx - 7.2, cy + 7.5);
    cairo_line_to(cr, cx - 7.2, cy - 0.5);
    cairo_line_to(cr, cx - 8.5, cy - 0.5);
    cairo_close_path(cr);
    cairo_fill(cr);
    break; }
  case PHONE_ICON_RECENTS: {
    double rad = 3.0;
    double x = cx - 7, y = cy - 7, w = 14, h = 14;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - rad, y + rad, rad, -M_PI_2, 0);
    cairo_arc(cr, x + w - rad, y + h - rad, rad, 0, M_PI_2);
    cairo_arc(cr, x + rad, y + h - rad, rad, M_PI_2, M_PI);
    cairo_arc(cr, x + rad, y + rad, rad, M_PI, 3 * M_PI_2);
    cairo_close_path(cr);
    cairo_set_line_width(cr, 1.9);
    cairo_stroke(cr);
    // inner smaller square
    rad = 1.6;
    x = cx - 4.5; y = cy - 4.5; w = 9; h = 9;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - rad, y + rad, rad, -M_PI_2, 0);
    cairo_arc(cr, x + w - rad, y + h - rad, rad, 0, M_PI_2);
    cairo_arc(cr, x + rad, y + h - rad, rad, M_PI_2, M_PI);
    cairo_arc(cr, x + rad, y + rad, rad, M_PI, 3 * M_PI_2);
    cairo_close_path(cr);
    cairo_set_line_width(cr, 1.2);
    cairo_stroke(cr);
    break; }
  case PHONE_ICON_KEYBOARD: {
    double rad = 2.2;
    double x = cx - 9, y = cy - 6, w = 18, h = 12;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - rad, y + rad, rad, -M_PI_2, 0);
    cairo_arc(cr, x + w - rad, y + h - rad, rad, 0, M_PI_2);
    cairo_arc(cr, x + rad, y + h - rad, rad, M_PI_2, M_PI);
    cairo_arc(cr, x + rad, y + rad, rad, M_PI, 3 * M_PI_2);
    cairo_close_path(cr);
    cairo_set_line_width(cr, 1.7);
    cairo_stroke(cr);
    for (int row = 0; row < 2; row++) {
      for (int col = 0; col < 3; col++) {
        double kx = cx - 6.5 + col * 4.4;
        double ky = cy - 2.8 + row * 3.6;
        cairo_arc(cr, kx+1.1, ky+1.1, 1.1, 0, 2*M_PI);
        cairo_fill(cr);
      }
    }
    break; }
  case PHONE_ICON_MOUSE: {
    double rad = 5;
    double x = cx - 6.5, y = cy - 8, w = 13, h = 16;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - rad, y + rad, rad, -M_PI_2, 0);
    cairo_arc(cr, x + w - rad, y + h - rad, rad, 0, M_PI_2);
    cairo_arc(cr, x + rad, y + h - rad, rad, M_PI_2, M_PI);
    cairo_arc(cr, x + rad, y + rad, rad, M_PI, 3 * M_PI_2);
    cairo_close_path(cr);
    cairo_set_line_width(cr, 1.7);
    cairo_stroke(cr);
    cairo_move_to(cr, cx, y);
    cairo_line_to(cr, cx, y + 5);
    cairo_set_line_width(cr, 1.2);
    cairo_stroke(cr);
    break; }
  default: break;
  }
  cairo_destroy(cr);
  cairo_surface_destroy(cairo_surf);
  return &buf->base;
}

// On-Screen-Tastatur-Taste rendern (abgerundetes Rechteck + zentrierter Text)
struct wlr_buffer *phone_render_key(struct wlr_allocator *allocator,
    const char *label, int width, int height, int ptsize,
    float bg_r, float bg_g, float bg_b, float fg_r, float fg_g, float fg_b) {
  (void)allocator;

  // Speicher für den Pixel-Buffer allozieren
  struct pixel_buf *buf = calloc(1, sizeof(struct pixel_buf));
  if (!buf) return NULL;

  // Stride = Breite * 4 Bytes (ARGB)
  buf->stride = width * 4;
  // Pixeldaten-Puffer allocieren
  buf->data = calloc(1, buf->stride * height);
  if (!buf->data) {
    free(buf);
    return NULL;
  }

  // wlr_buffer initialisieren mit unseren Callbacks
  wlr_buffer_init(&buf->base, &pixel_buf_impl, width, height);

  // Cairo-Oberfläche auf unseren Pixel-Puffer legen
  cairo_surface_t *cairo_surf = cairo_image_surface_create_for_data(
      buf->data, CAIRO_FORMAT_ARGB32, width, height, buf->stride);
  cairo_t *cr = cairo_create(cairo_surf);

  // Hintergrund komplett transparent machen
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgba(cr, 0, 0, 0, 0);
  cairo_paint(cr);

  // Abgerundetes Rechteck als Tasten-Hintergrund
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  double radius = 8;
  double x = 2, y = 2, w = width - 4, h = height - 4;
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - radius, y + radius, radius, -M_PI_2, 0);
  cairo_arc(cr, x + w - radius, y + h - radius, radius, 0, M_PI_2);
  cairo_arc(cr, x + radius, y + h - radius, radius, M_PI_2, M_PI);
  cairo_arc(cr, x + radius, y + radius, radius, M_PI, 3 * M_PI_2);
  cairo_close_path(cr);
  cairo_set_source_rgba(cr, bg_r, bg_g, bg_b, 0.9);
  cairo_fill(cr);

  // Label zentriert zeichnen (falls vorhanden)
  if (label && label[0]) {
    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *font_desc = pango_font_description_new();
    pango_font_description_set_family(font_desc, "sans-serif");
    pango_font_description_set_absolute_size(font_desc, ptsize * PANGO_SCALE);
    pango_layout_set_font_description(layout, font_desc);
    pango_font_description_free(font_desc);
    pango_layout_set_text(layout, label, -1);
    PangoRectangle ink;
    pango_layout_get_pixel_extents(layout, &ink, NULL);
    cairo_set_source_rgba(cr, fg_r, fg_g, fg_b, 1.0);
    cairo_move_to(cr, (width - ink.width) / 2.0, (height - 4) / 2.0 - ink.y);
    pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);
  }

  cairo_destroy(cr);
  cairo_surface_destroy(cairo_surf);

  // Fertigen Buffer zurückgeben
  return &buf->base;
}

// Abgerundetes Rechteck — solid matte, soft shadow + hairline border (no liquid glass)
struct wlr_buffer *phone_render_rounded_rect(struct wlr_allocator *allocator,
    int width, int height, float radius,
    float r, float g, float b, float a) {
  (void)allocator;
  struct pixel_buf *buf = calloc(1, sizeof(struct pixel_buf));
  if (!buf) return NULL;
  buf->stride = width * 4;
  buf->data = calloc(1, buf->stride * height);
  if (!buf->data) { free(buf); return NULL; }
  wlr_buffer_init(&buf->base, &pixel_buf_impl, width, height);
  cairo_surface_t *cairo_surf = cairo_image_surface_create_for_data(
      buf->data, CAIRO_FORMAT_ARGB32, width, height, buf->stride);
  cairo_t *cr = cairo_create(cairo_surf);
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgba(cr, 0, 0, 0, 0);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  if (radius < 0) radius = 0;
  if (radius > width / 2.0) radius = width / 2.0;
  if (radius > height / 2.0) radius = height / 2.0;
  double x = 0, y = 0, w = width, h = height;
  // soft shadow only for opaque cards
  if (a > 0.92f && width > 28 && height > 28) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - radius, y + radius + 1.4, radius, -M_PI_2, 0);
    cairo_arc(cr, x + w - radius, y + h - radius + 1.4, radius, 0, M_PI_2);
    cairo_arc(cr, x + radius, y + h - radius + 1.4, radius, M_PI_2, M_PI);
    cairo_arc(cr, x + radius, y + radius + 1.4, radius, M_PI, 3 * M_PI_2);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.16);
    cairo_fill(cr);
  }
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - radius, y + radius, radius, -M_PI_2, 0);
  cairo_arc(cr, x + w - radius, y + h - radius, radius, 0, M_PI_2);
  cairo_arc(cr, x + radius, y + h - radius, radius, M_PI_2, M_PI);
  cairo_arc(cr, x + radius, y + radius, radius, M_PI, 3 * M_PI_2);
  cairo_close_path(cr);
  cairo_set_source_rgba(cr, r, g, b, a);
  cairo_fill(cr);
  // hairline border for definition on wallpaper — not glossy
  if (width > 20 && height > 12) {
    cairo_set_source_rgba(cr, 0, 0, 0, 0.10);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
  }
  cairo_destroy(cr);
  cairo_surface_destroy(cairo_surf);
  return &buf->base;
}
struct wlr_buffer *phone_render_glass(struct wlr_allocator *allocator,
    int width, int height, float radius,
    float r, float g, float b, float a) {
  // keep API but render as solid matte (no translucency highlight)
  if (a < 0.96f) a = 0.97f;
  return phone_render_rounded_rect(allocator, width, height, radius, r, g, b, a);
}
struct wlr_buffer *phone_render_pill(struct wlr_allocator *allocator,
    int width, int height,
    float r, float g, float b, float a) {
  float rad = height / 2.0f;
  return phone_render_rounded_rect(allocator, width, height, rad, r, g, b, a);
}
