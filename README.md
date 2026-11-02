# Phone WM (Vulkan)

Second attempt of the phone window manager: **the same WM as `phone/wm`, but
rendering through the wlroots 0.20 Vulkan renderer** instead of the default
GLES2/EGL path.

This is an experiment to test hardware-accelerated compositing via Vulkan.

## What's different vs. the original

| | Original `phone/wm` | This one (`phone-wm-vulkan`) |
|---|---|---|
| Renderer | `wlr_renderer_autocreate()` → GLES2/EGL (or pixman) | `wlr_vk_renderer_create_with_drm_fd()` → Vulkan |
| UI buffers | Software (CPU) pixel buffers, uploaded to GPU | Same software buffers, uploaded via Vulkan |
| Output buffers | GBM (dma-buf) | GBM (dma-buf), imported into Vulkan |
| Fallback | none | auto: Vulkan → GLES2 → pixman |

The allocator is still `wlr_allocator_autocreate()` — it picks the **GBM**
allocator because the Vulkan renderer supports `WLR_BUFFER_CAP_DMABUF`, so
render targets stay on the GPU. `wlr_scene` handles direct scanout to KMS
planes as before.

## Building

Requires wlroots-0.20 built with the Vulkan renderer
(`-Dvulkan=enabled`, Arch package `wlroots0.20` ships it).

```sh
meson setup build
ninja -C build
```

## Running

```sh
# Default: Vulkan renderer (on DRM/KMS it will pick up your GPU)
./build/phone-wm-vulkan

# Force a specific renderer for comparison/testing:
./build/phone-wm-vulkan vulkan   # explicit Vulkan
./build/phone-wm-vulkan gles2    # GLES2/EGL (original behaviour)
./build/phone-wm-vulkan auto     # wlr_renderer_autocreate()

# Nested session for testing:
WLR_BACKENDS=wayland ./build/phone-wm-vulkan

# Headless:
WLR_BACKENDS=headless ./build/phone-wm-vulkan
```

When Vulkan cannot initialize (e.g. headless backend without a DRM fd), the WM
logs `Failed to create Vulkan renderer, falling back to auto` and continues
with GLES2/pixman. You can confirm which renderer is active from the startup
log (`Using Vulkan renderer (GPU hardware acceleration)` vs
`Using non-Vulkan renderer`).

## Features

| Feature | Status |
|---------|--------|
| Wayland compositor core | Done |
| XDG Shell (app windows) | Done |
| Layer Shell (status bar) | Done |
| Touch input + gestures | Done |
| Multi-workspace (5) | Done |
| Built-in status bar | Done |
| App launcher overlay | Done |
| Control Center (quick settings) | Done |
| Notification shade | Placeholder |

## Control Center

Android-style quick-settings shade:

- **Open:** swipe down starting from the top edge of the screen
- **Close:** swipe up, or tap on the dimmed area below the panel
- **Tiles:** WLAN, Bluetooth, Flugmodus, Taschenlampe — tap to toggle the real
  hardware (see below)
- **Sliders:** Helligkeit & Lautstaerke — tap on the track to set the real value
- **Info line:** current time and battery state

Opens on top of the launcher/recents and closes them (and vice versa).

### Hardware control backends

| Toggle | Read state | Write state |
|--------|-----------|-------------|
| WLAN | `nmcli -t radio` | `nmcli radio wifi on\|off` |
| Bluetooth | `bluetoothctl show` (`Powered:`) | `bluetoothctl power on\|off` |
| Flugmodus | WLAN + Bluetooth both off | `nmcli radio all off\|on` + `bluetoothctl power on\|off` |
| Taschenlampe | `/sys/class/leds/*/brightness` (flash/torch LED, writable) | write `max_brightness` / `0` |
| Helligkeit | `/sys/class/backlight/*/brightness` + `max_brightness` | write `brightness` |
| Lautstärke | `pactl get-sink-volume @DEFAULT_SINK@` | `pactl set-sink-volume @DEFAULT_SINK@ N%` |

On machines without a given backend (e.g. no writable torch LED, no backlight,
no audio server) the corresponding tile/slider simply stays inactive and taps
are logged — no error spam.

## Notes

- The wlroots Vulkan renderer is still marked experimental upstream.
- Fullscreen surfaces should use **direct scanout** (no GPU compositing at
  all) thanks to the scene-graph API — identical to the GLES2 build.
- Debug with `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` and
  `WLR_DRM_DEBUG`/`WLR_SCENE_DEBUG_DAMAGE`.

## Dependencies

- `wlroots0.20` (with Vulkan renderer), `vulkan` (headers + ICD)
- `wayland`, `wayland-server`, `libinput`, `libxkbcommon`
- `pixman`, `mesa`, `cairo`, `pangocairo`
- `meson`, `ninja`, `gcc`
