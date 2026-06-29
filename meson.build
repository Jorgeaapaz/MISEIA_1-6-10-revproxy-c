project('proxy', 'c',
  version : '0.1.0',
  default_options : [
    'c_std=c11',
    'warning_level=2',
  ],
)

cc = meson.get_compiler('c')

# ── Dependencies ────────────────────────────────────────────────────────────
thread_dep = dependency('threads')

# ── Platform sources ────────────────────────────────────────────────────────
if host_machine.system() == 'windows'
  platform_src  = files('platform/iocp.c')
  ws2_dep       = cc.find_library('ws2_32', required : true)
  mswsock_dep   = cc.find_library('mswsock', required : true)
  platform_deps = [ws2_dep, mswsock_dep]
else
  platform_src  = files('platform/epoll.c')
  platform_deps = []
endif

# ── Subdirectories ──────────────────────────────────────────────────────────
subdir('src')
subdir('tests')

# ── Code formatting (clang-format) ──────────────────────────────────────────
clang_format = find_program('clang-format', required : false)
if clang_format.found()
  all_c_sources = files(
    'src/main.c', 'src/config.c', 'src/router.c', 'src/balancer.c',
    'src/dispatcher.c', 'src/listener.c', 'src/log.c', 'src/utils.c',
    'platform/epoll.c',
  )
  run_target('format',
    command : [clang_format, '--style=file', '-i', all_c_sources],
  )
  run_target('format-check',
    command : [clang_format, '--style=file', '--dry-run', '--Werror', all_c_sources],
  )
endif
