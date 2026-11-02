project('phone-wm-vulkan', 'c',
  version: '0.1.0',
  license: 'MIT',
  default_options: ['warning_level=3', 'c_std=c11'],
)

cc = meson.get_compiler('c')

wlroots = dependency('wlroots-0.20')
wayland_server = dependency('wayland-server')
wayland_protos = dependency('wayland-protocols', required: false)
xkbcommon = dependency('xkbcommon')
libinput = dependency('libinput')
pixman = dependency('pixman-1')
vulkan = dependency('vulkan')
math = cc.find_library('m', required: false)

# Der Vulkan-Renderer muss in wlroots einkompiliert sein
if wlroots.get_variable(pkgconfig: 'have_vulkan_renderer', default_value: '0') != 'true'
  error('wlroots-0.20 was built without the Vulkan renderer')
endif
wl_scanner = find_program('wayland-scanner')

wlr_protocol_dir = get_option('wlr-protocol-dir')
if wlr_protocol_dir == ''
  wlr_protos_dep = dependency('wlr-protocols', required: false)
  if wlr_protos_dep.found()
    wlr_protocol_dir = wlr_protos_dep.get_variable('pkgdatadir')
  endif
endif

if wlr_protocol_dir == ''
  wlr_protocol_dir = '/usr/share/wlr-protocols'
endif

layer_shell_xml = wlr_protocol_dir / 'unstable/wlr-layer-shell-unstable-v1.xml'

layer_shell_proto_h = custom_target(
  'wlr-layer-shell-protocol-h',
  input: layer_shell_xml,
  output: 'wlr-layer-shell-unstable-v1-protocol.h',
  command: [wl_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
)

cairo = dependency('cairo')
pangocairo = dependency('pangocairo')

src_files = [
  'src/main.c',
  'src/server.c',
  'src/output.c',
  'src/input.c',
  'src/gesture.c',
  'src/shell.c',
  'src/layers.c',
  'src/bar.c',
  'src/statusbar.c',
  'src/launcher.c',
  'src/render.c',
  'src/recents.c',
  'src/controlcenter.c',
  'src/osk.c',
  'src/hw.c',
  'src/theme.c',
  'src/notify.c',
  'src/power.c',
  'src/lock.c',
  'src/toast.c',
  'src/osd.c',
  'src/settings.c',
  'src/anim.c',
]

executable(
  'phone-wm-vulkan',
  src_files + [layer_shell_proto_h],
  dependencies: [
    wlroots,
    wayland_server,
    xkbcommon,
    libinput,
    pixman,
    vulkan,
    math,
    cairo,
    pangocairo,
  ],
  c_args: ['-DWLR_USE_UNSTABLE'],
  install: true,
)
