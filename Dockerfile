FROM ubuntu:24.04 AS builder

RUN apt-get update -qq && \
    apt-get install -y --no-install-recommends \
        meson ninja-build gcc pkg-config && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN meson setup builddir -Dbuildtype=release && \
    meson compile -C builddir

# ── Runtime image ──────────────────────────────────────────────────────────
FROM ubuntu:24.04

RUN apt-get update -qq && \
    apt-get install -y --no-install-recommends libgcc-s1 && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /src/builddir/proxy /app/proxy
COPY proxy.toml.example /app/proxy.toml

EXPOSE 8080

ENTRYPOINT ["/app/proxy", "/app/proxy.toml"]
