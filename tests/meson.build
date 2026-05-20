# ── Tests ────────────────────────────────────────────────────────────────────

# Try to use Unity as a subproject; fall back gracefully if unavailable.
unity_dep = dependency('unity', fallback : ['unity', 'unity_dep'], required : false)

# ── Unit test executables ────────────────────────────────────────────────────

test_router_exe = executable('test_router',
  files('test_router.c'),
  dependencies : proxy_core_dep,
  include_directories : include_directories('..'),
)

test_balancer_exe = executable('test_balancer',
  files('test_balancer.c'),
  dependencies : proxy_core_dep,
  include_directories : include_directories('..'),
)

test_config_exe = executable('test_config',
  files('test_config.c'),
  dependencies : proxy_core_dep,
  include_directories : include_directories('..'),
)

# ── Mock server (used by integration tests) ──────────────────────────────────

mock_server_exe = executable('mock_server',
  files('mock_server.c'),
  dependencies : host_machine.system() == 'windows' ?
    [cc.find_library('ws2_32', required : true)] : [],
  include_directories : include_directories('..'),
)

# ── Register tests with Meson ─────────────────────────────────────────────────

test('router',   test_router_exe,   suite : 'unit')
test('balancer', test_balancer_exe, suite : 'unit')
test('config',   test_config_exe,   suite : 'unit')
