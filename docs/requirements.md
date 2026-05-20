# Requisitos: Proxy Inverso de Alto Rendimiento (epoll/IOCP)

## REQ-F — Requisitos Funcionales

### REQ-F-01 · Escucha en múltiples puertos

- El proxy abrirá uno o más sockets TCP de escucha, cada uno en el puerto indicado en la configuración.
- El conjunto de listeners es dinámico: la recarga de configuración puede añadir o eliminar listeners sin reiniciar el proceso.

**Criterio de aceptación**: con `[[listener]] port=80` y `[[listener]] port=443` activos, peticiones a ambos puertos son aceptadas y enrutadas correctamente.

---

### REQ-F-02 · Enrutamiento por nombre de dominio (Layer 7)

- Al aceptar una conexión TCP, el proxy lee los bytes de cabecera HTTP hasta encontrar la línea `Host:`.
- Normaliza el valor a minúsculas y elimina el puerto si viene incluido (p. ej. `api.example.com:8080` → `api.example.com`).
- Busca en la tabla de rutas según la siguiente precedencia:
  1. Coincidencia exacta (`api.example.com`).
  2. Comodín de subdominio (`*.example.com`).
  3. Comodín global (`*`).
- Si no se encuentra ninguna regla, responde `502 Bad Gateway` y cierra la conexión.

**Criterio de aceptación**: peticiones con `Host: api.test`, `Host: sub.web.test` y `Host: unknown` son enrutadas/rechazadas según las reglas anteriores.

---

### REQ-F-03 · Balanceo de carga round-robin

- Cada regla de dominio tiene asociada una lista de N backends (`host:puerto`).
- La selección del backend para cada nueva conexión se realiza en round-robin atómico (sin lock global) usando `_Atomic int`.
- El índice avanza aunque haya múltiples workers concurrentes.

**Criterio de aceptación**: 10 conexiones consecutivas al mismo dominio con 2 backends distribuyen ~5 peticiones a cada uno.

---

### REQ-F-04 · Reenvío bidireccional transparente

- Los bytes recibidos del cliente (incluyendo los ya leídos durante la extracción del `Host`) se reenvían íntegramente al backend.
- Los bytes de respuesta del backend se reenvían íntegramente al cliente.
- No se modifica el body ni se recomprime.
- Opcionalmente (configurable) se inyecta la cabecera `X-Forwarded-For: <IP-cliente>` antes de reenviar al backend.
- Se honra HTTP keep-alive si ambos extremos lo soportan; en caso contrario se cierra la conexión tras la primera respuesta.

**Criterio de aceptación**: `curl` a través del proxy obtiene la misma respuesta que conectando directamente al backend.

---

### REQ-F-05 · Lectura de cabecera con límite y timeout

- El buffer de lectura de cabecera tiene un tamaño máximo de 8 192 bytes.
- Si el cliente no envía datos completos en el plazo `read_timeout_ms`, la conexión se cierra con `408 Request Timeout`.
- Si la cabecera supera 8 192 bytes sin aparecer `\r\n\r\n`, la conexión se cierra con `431 Request Header Fields Too Large`.

**Criterio de aceptación**: clientes lentos o con cabeceras gigantes son rechazados en los plazos indicados.

---

### REQ-F-06 · Conexión al backend con timeout

- El intento de conectar al backend tiene un plazo máximo de `connect_timeout_ms`.
- Si el backend no responde en ese plazo, se responde `504 Gateway Timeout` al cliente.

**Criterio de aceptación**: bloqueando un backend con `iptables DROP`, el proxy responde 504 dentro del plazo configurado.

---

### REQ-F-07 · Recarga dinámica de configuración

- En Linux: el proceso reacciona a la señal `SIGHUP`.
- En Windows: el proceso reacciona al evento nombrado `proxy-reload` (creado al arrancar con `CreateEvent`).
- Al recibir la señal/evento: relee `proxy.toml`, valida la nueva configuración. Si es válida, sustituye la tabla de rutas de forma atómica (`_Atomic` pointer swap). Si no es válida, registra el error y mantiene la configuración anterior.
- Las conexiones activas en el momento de la recarga no se interrumpen.

**Criterio de aceptación**: enviar `SIGHUP` mientras hay conexiones en curso; las conexiones activas terminan normalmente y las nuevas usan la configuración recargada. Latencia de recarga < 50 ms.

---

### REQ-F-08 · Logging estructurado

- Cada conexión completada emite una línea de log con el formato:
  ```
  <ISO8601> [<LEVEL>] [<thread>] <IP>:<port> → <domain> → <backend>:<port> (<ms> ms)
  ```
- Niveles soportados: `trace`, `debug`, `info`, `warn`, `error`.
- La salida por defecto es `stderr`; el flag `--log-file <path>` redirige a fichero.
- El nivel se configura en `proxy.toml` y se puede cambiar sin reiniciar (aplicado en la siguiente recarga).

**Criterio de aceptación**: en nivel `info`, cada petición produce exactamente una línea de log con todos los campos requeridos.

---

### REQ-F-09 · Configuración mediante fichero TOML

- El fichero de configuración es `proxy.toml` (ruta overridable con `--config <path>`).
- Secciones obligatorias: al menos un `[[listener]]` y al menos una `[[route]]`.
- Sección `[global]` opcional; valores por defecto aplicados si se omite.
- El proceso rechaza arrancar si `proxy.toml` no existe o contiene errores de sintaxis/semántica.

**Criterio de aceptación**: fichero inválido → proceso termina con código de salida ≠ 0 y mensaje de error descriptivo en stderr.

---

## REQ-NF — Requisitos No Funcionales

### REQ-NF-01 · Rendimiento — Latencia

- La latencia añadida por el proxy (tiempo proxy − tiempo directo) no superará **1 ms en el percentil 99** para tráfico en LAN.

---

### REQ-NF-02 · Rendimiento — Concurrencia

- El sistema soportará al menos **10 000 conexiones simultáneas** con el límite de descriptores ajustado (`ulimit -n 65536`).

---

### REQ-NF-03 · Plataforma — Linux

- Compilable con GCC ≥ 11 y Clang ≥ 14.
- Usa `epoll_create1(EPOLL_CLOEXEC)` en modo edge-triggered (`EPOLLET`).
- Un hilo dedicado a `accept()`; N worker threads cada uno con su propio `epoll` fd.

---

### REQ-NF-04 · Plataforma — Windows

- Compilable con MSVC 2022 o Clang/LLVM para Windows.
- Usa `CreateIoCompletionPort` + `GetQueuedCompletionStatusEx`.
- El pool de hilos se construye sobre el thread pool del sistema (o hilos propios vinculados al puerto IOCP).

---

### REQ-NF-05 · Estándar de lenguaje

- Código C11 (`-std=c11`). No se usarán extensiones no portables salvo las encapsuladas en `platform/`.
- Cero warnings con `-Wall -Wextra -Wpedantic` en GCC/Clang.

---

### REQ-NF-06 · Gestión del proyecto

- El sistema de build es **Meson ≥ 1.3**.
- Los comandos `meson setup`, `meson compile` y `meson test` deben funcionar sin pasos manuales adicionales.
- Las dependencias externas (`tomlc99`, `unity`) se gestionan con Meson wraps.

---

### REQ-NF-07 · Seguridad básica

- Ningún buffer de entrada de usuario (cabecera HTTP, fichero de configuración) puede provocar un desbordamiento de pila o heap.
- Los descriptores de fichero de clientes se cierran correctamente en todos los caminos de error.
- El proceso no necesita ejecutarse como root salvo para puertos < 1024 (documentado).

---

### REQ-NF-08 · Observabilidad

- Al arrancar, el proceso emite en `info` la configuración activa: puertos en escucha, número de rutas, número de workers.
- Al recargar la configuración, emite el número de rutas añadidas/eliminadas/sin cambio.

---

## REQ-T — Requisitos de Tests

### REQ-T-01 · Test unitario: Router

| ID | Caso |
|---|---|
| T-01-1 | Coincidencia exacta devuelve la ruta correcta |
| T-01-2 | Comodín `*.example.com` coincide con `sub.example.com` |
| T-01-3 | Comodín `*.example.com` no coincide con `example.com` |
| T-01-4 | Comodín global `*` captura dominio sin regla |
| T-01-5 | Sin ninguna regla ni comodín → devuelve NULL |
| T-01-6 | Coincidencia exacta tiene precedencia sobre comodín |

---

### REQ-T-02 · Test unitario: Balancer

| ID | Caso |
|---|---|
| T-02-1 | Round-robin sobre 3 backends cicla correctamente 0→1→2→0 |
| T-02-2 | Con 1 backend, siempre devuelve ese mismo |
| T-02-3 | 4 goroutines (threads) concurrentes no producen duplicados (thread-safety) |

---

### REQ-T-03 · Test unitario: Config

| ID | Caso |
|---|---|
| T-03-1 | Fichero TOML mínimo válido carga sin errores |
| T-03-2 | Falta de `[[listener]]` → error de validación |
| T-03-3 | Falta de `[[route]]` → error de validación |
| T-03-4 | Puerto fuera de rango (0 o > 65535) → error |
| T-03-5 | Backend con formato inválido → error |
| T-03-6 | Valores de `[global]` por defecto aplicados cuando se omite la sección |

---

### REQ-T-04 · Test de integración

Escenario ejecutado por `tests/run_integration.sh` (Linux) / `tests/run_integration.ps1` (Windows):

| Paso | Acción | Verificación |
|---|---|---|
| 1 | Generar `proxy.toml` con 3 dominios (`api.test`, `web.test`, `admin.test`), 2 backends cada uno | Fichero creado y sintácticamente válido |
| 2 | Lanzar 6 mini-servidores HTTP que devuelven su nombre en el body | `curl 127.0.0.1:<puerto>` responde con el nombre del servidor |
| 3 | Añadir entradas en `hosts` (`127.0.0.1 api.test`, etc.) | `ping api.test` resuelve a 127.0.0.1 |
| 4 | Arrancar el proxy | Proceso en ejecución, puertos en escucha |
| 5 | `curl -H "Host: api.test" http://127.0.0.1:8080/ping` | HTTP 200, body identifica uno de los backends de api |
| 6 | `curl -H "Host: unknown" http://127.0.0.1:8080/ping` | HTTP 502 |
| 7 | 10 peticiones a `api.test` | Cada backend recibe entre 4 y 6 peticiones |
| 8 | Modificar `proxy.toml` (cambiar backend de `web.test`) + `SIGHUP` | Sin reinicio; peticiones a `web.test` llegan al nuevo backend |
| 9 | Detener el proxy | Proceso termina limpiamente (exit 0) |

---

## REQ-C — Restricciones y Decisiones de Diseño

| ID | Restricción |
|---|---|
| REQ-C-01 | Lenguaje: C11. No C++, no Rust, no scripts de glue salvo en tests. |
| REQ-C-02 | Tamaño máximo del buffer de cabecera: 8 192 bytes (no configurable en tiempo de ejecución). |
| REQ-C-03 | El parser HTTP es mínimo (solo extrae `Host:`). No se valida el método, versión ni otras cabeceras. |
| REQ-C-04 | No se implementa TLS en la fase inicial; se reserva en Fase 7 usando mbedTLS. |
| REQ-C-05 | Sin dependencias de red externas en tiempo de compilación salvo las wraps de Meson. |
| REQ-C-06 | El número máximo de rutas es 1 024 y el de backends por ruta es 64 (límites de array estático en v1). |
| REQ-C-07 | El proceso escribe logs solo en `stderr` o en el fichero indicado; no usa syslog. |
