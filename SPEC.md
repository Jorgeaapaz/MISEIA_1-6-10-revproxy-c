# Especificación: Proxy Inverso de Alto Rendimiento (epoll/IOCP)

## 1. Visión General

Proxy inverso Layer 7 escrito en C, con modelo de E/S asíncrona nativa:
- **Linux**: `epoll` con `edge-triggered` mode.
- **Windows**: I/O Completion Ports (IOCP).

El proxy escucha en uno o varios puertos, inspecciona la cabecera HTTP `Host`, y reenvía la conexión al backend correcto según las reglas de dominio configuradas. Cada backend puede tener múltiples servidores con balanceo round-robin.

---

## 2. Arquitectura

```
Cliente
  │
  ▼
[Listener(s)]  ←── acepta conexiones en N puertos
  │
  ▼
[Dispatcher]   ←── lee cabecera HTTP, extrae Host
  │
  ▼
[Router]       ←── busca regla: dominio → lista de backends
  │
  ▼
[Load Balancer] ←── round-robin sobre la lista de backends
  │
  ▼
[Backend Connection] ←── reenvío bidireccional de bytes
```

### 2.1 Modelo de hilos

| Plataforma | Modelo |
|---|---|
| Linux | 1 hilo de aceptación + N worker threads con epoll fd propio (1 por hilo) |
| Windows | Thread pool del sistema asociado al IOCP completion port |

El número de workers es configurable; por defecto `nº_CPUs`.

### 2.2 Flujo de una conexión

1. `accept()` → nuevo fd de cliente.
2. Leer bytes hasta tener la primera línea de la cabecera HTTP (máximo 8 KB, timeout configurable).
3. Extraer cabecera `Host:` (normalizar a minúsculas, eliminar puerto si viene incluido).
4. Buscar en la tabla de rutas la entrada que coincida. Si no hay coincidencia → responder `502 Bad Gateway` y cerrar.
5. Seleccionar servidor backend (round-robin atómico).
6. Abrir conexión TCP al backend seleccionado.
7. Reenviar los bytes ya leídos + resto de la solicitud al backend; reenviar la respuesta al cliente.
8. Cerrar ambas conexiones cuando se detecte EOF o error.

---

## 3. Configuración

Fichero en formato **TOML** (`proxy.toml`).

### 3.1 Estructura

```toml
[global]
workers        = 4          # hilos worker (0 = nº CPUs)
connect_timeout_ms  = 5000  # timeout conexión a backend
read_timeout_ms     = 30000 # timeout lectura de cabecera cliente
log_level      = "info"     # trace | debug | info | warn | error

[[listener]]
port    = 80
tls     = false

[[listener]]
port    = 443
tls     = true
cert    = "certs/server.crt"
key     = "certs/server.key"

[[route]]
domain  = "api.example.com"
backends = [
  "192.168.1.10:8080",
  "192.168.1.11:8080",
]

[[route]]
domain  = "web.example.com"
backends = [
  "192.168.1.20:80",
]

[[route]]
domain  = "*"               # comodín: captura cualquier dominio no mapeado
backends = ["127.0.0.1:8000"]
```

### 3.2 Reglas de coincidencia de dominio

Precedencia (de mayor a menor):
1. Coincidencia exacta (`api.example.com`).
2. Comodín de subdominio (`*.example.com`).
3. Comodín global (`*`).

### 3.3 Recarga dinámica

- Señal `SIGHUP` en Linux.
- Evento nombrado (`proxy-reload`) en Windows.
- Al recibir la señal: releer `proxy.toml`, validar, y sustituir la tabla de rutas de forma atómica (RCU / `_Atomic` pointer swap) sin interrumpir conexiones activas.

---

## 4. Módulos del Código Fuente

```
src/
  main.c            — punto de entrada, bootstrap, señales
  config.c/.h       — parser TOML, validación, estructura Config
  listener.c/.h     — creación y gestión de sockets de escucha
  router.c/.h       — tabla de rutas, búsqueda de dominio
  balancer.c/.h     — round-robin atómico por grupo de backends
  dispatcher.c/.h   — lectura de cabecera HTTP, extracción de Host
  tunnel.c/.h       — reenvío bidireccional de bytes (splice/sendfile)
  event_loop.c/.h   — abstracción epoll (Linux) / IOCP (Windows)
  log.c/.h          — logging con niveles y timestamp
  utils.c/.h        — helpers: strings, tiempo, plataforma
platform/
  epoll.c/.h        — implementación Linux
  iocp.c/.h         — implementación Windows
tests/
  test_router.c
  test_balancer.c
  test_config.c
  test_integration.c
```

---

## 5. API Interna (tipos clave)

```c
/* Configuración cargada en memoria */
typedef struct {
    int       workers;
    int       connect_timeout_ms;
    int       read_timeout_ms;
    LogLevel  log_level;
    Listener *listeners;   /* array, terminado en {0} */
    Route    *routes;      /* array, terminado en {0} */
} Config;

/* Una entrada de escucha */
typedef struct {
    uint16_t port;
    bool     tls;
    char     cert[256];
    char     key[256];
} Listener;

/* Una regla de enrutamiento */
typedef struct {
    char      domain[253];     /* FQDN o comodín */
    char    **backends;        /* array terminado en NULL */
    int       nbackends;
    _Atomic int rr_index;      /* índice round-robin */
} Route;

/* Contexto de una conexión cliente↔backend */
typedef struct {
    int      client_fd;
    int      backend_fd;
    Route   *route;
    State    state;            /* READING_HEADER | TUNNELING | CLOSING */
    uint8_t  hdr_buf[8192];
    int      hdr_len;
} Conn;
```

---

## 6. Abstracción de Event Loop

```c
/* Inicializar el event loop para este hilo */
EventLoop *event_loop_create(void);

/* Registrar fd para lectura/escritura */
int event_loop_add(EventLoop *el, int fd, uint32_t events, void *ctx);
int event_loop_mod(EventLoop *el, int fd, uint32_t events, void *ctx);
int event_loop_del(EventLoop *el, int fd);

/* Esperar eventos (timeout en ms, -1 = infinito) */
int event_loop_wait(EventLoop *el, Event *out, int maxevents, int timeout_ms);

/* Liberar */
void event_loop_destroy(EventLoop *el);
```

En Linux implementado sobre `epoll_create1` / `epoll_ctl` / `epoll_wait`.  
En Windows sobre `CreateIoCompletionPort` / `GetQueuedCompletionStatusEx`.

---

## 7. Protocolo de Reenvío

- Se reenvía la petición HTTP **sin modificar** (proxy transparente L7).
- Opcionalmente se añade cabecera `X-Forwarded-For` con la IP del cliente (configurable).
- No hay descompresión/recompresión de body.
- Keep-alive: se honra si tanto el cliente como el backend lo soportan; en otro caso se cierra tras la primera respuesta.

---

## 8. Gestión del Proyecto — Meson

```
meson.build         — raíz
src/meson.build
tests/meson.build
```

### Opciones de build

```bash
# Configurar
meson setup builddir -Dbuildtype=release

# Compilar
meson compile -C builddir

# Tests
meson test -C builddir

# Con cobertura
meson setup builddir -Db_coverage=true
meson test -C builddir
ninja -C builddir coverage-html
```

### Dependencias externas (wrap)

| Biblioteca | Uso |
|---|---|
| `tomlc99` | Parser TOML |
| `unity` | Framework de test unitario |

---

## 9. Suite de Tests

### 9.1 Tests unitarios

| Fichero | Qué prueba |
|---|---|
| `test_router.c` | Coincidencia exacta, comodín, sin match |
| `test_balancer.c` | Round-robin correcto, thread-safety |
| `test_config.c` | Parsing TOML válido e inválido |

### 9.2 Test de integración

Escenario completo ejecutable con un script (`tests/run_integration.sh` / `.ps1`):

1. **Generar configuración de prueba** (`proxy.toml` con 3 dominios, 2 backends cada uno).
2. **Lanzar servidores de backend simulados** — mini servidores HTTP en C (o `ncat -l`) que devuelven su propio nombre en la respuesta.
3. **Registrar dominios en `hosts`**:
   - Linux: `/etc/hosts`
   - Windows: `C:\Windows\System32\drivers\etc\hosts`
   - Entradas: `127.0.0.1 api.test`, `127.0.0.1 web.test`, `127.0.0.1 admin.test`
4. **Arrancar el proxy**.
5. **Verificar** con `curl`:
   ```bash
   curl -H "Host: api.test"   http://127.0.0.1:8080/ping  # → backend-api-1 o api-2
   curl -H "Host: web.test"   http://127.0.0.1:8080/ping  # → backend-web-1
   curl -H "Host: unknown"    http://127.0.0.1:8080/ping  # → 502
   ```
6. **Test de recarga**: modificar `proxy.toml` y enviar `SIGHUP` / evento; verificar que el nuevo enrutado está activo sin reiniciar.
7. **Test de round-robin**: 10 peticiones al mismo dominio con 2 backends; verificar que cada backend recibe ~5.

---

## 10. Logging

Formato de línea:

```
2026-05-02T14:23:01.123Z [INFO ] [worker-2] 10.0.0.5:54321 → api.test → 192.168.1.10:8080 (12 ms)
```

Campos: timestamp ISO8601, nivel, hilo, IP cliente, dominio, backend elegido, latencia.

Salida a `stderr` por defecto; redirigible a fichero con `--log-file`.

---

## 11. Requisitos No Funcionales

| Requisito | Objetivo |
|---|---|
| Latencia añadida | < 1 ms p99 en LAN |
| Conexiones simultáneas | ≥ 10 000 (ajustando `ulimit -n`) |
| Recarga de config | < 50 ms, sin perder conexiones activas |
| Compilación cruzada | GCC en Linux, MSVC/Clang en Windows |
| Estándar C | C11 |

---

## 12. Fases de Implementación

| Fase | Contenido |
|---|---|
| 1 | Scaffolding Meson + módulos vacíos + CI básico |
| 2 | Event loop (epoll primero) + listener + accept |
| 3 | Parser de cabecera HTTP + extracción de Host |
| 4 | Router + balancer + tunnel básico |
| 5 | Recarga dinámica de configuración |
| 6 | Port a Windows (IOCP) |
| 7 | TLS (opcional, usando mbedTLS) |
| 8 | Suite de tests de integración completa |
