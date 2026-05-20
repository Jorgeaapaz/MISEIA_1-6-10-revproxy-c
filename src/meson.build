# ── Core library sources (everything except main.c) ────────────────────────
proxy_core_src = [
  files('log.c'),
  files('utils.c'),
  files('config.c'),
  files('router.c'),
  files('balancer.c'),
  files('listener.c'),
  files('dispatcher.c'),
  files('tunnel.c'),
] + platform_src

proxy_core_inc = [
  include_directories('.'),
  include_directories('..'),
]

# ── Static library used by both the proxy executable and tests ───────────────
proxy_core = static_library('proxy_core',
  proxy_core_src,
  include_directories : proxy_core_inc,
  dependencies        : [thread_dep] + platform_deps,
  c_args              : [],
)

# Export as a dependency object so tests can link easily
proxy_core_dep = declare_dependency(
  link_with           : proxy_core,
  include_directories : proxy_core_inc,
  dependencies        : [thread_dep] + platform_deps,
)

# ── Proxy executable ─────────────────────────────────────────────────────────
executable('proxy',
  files('main.c'),
  include_directories : proxy_core_inc,
  link_with           : proxy_core,
  dependencies        : [thread_dep] + platform_deps,
  install             : true,
)
