# canduet-server: connection server + lesson audio relay (doc/TINCAN-RELAY.md, D-025).
# Build from the repo root:  docker build -f server/docker/Dockerfile -t canduet-server .
# Run (host networking: the relay uses a UDP port range):
#   docker run --rm --network host canduet-server --relay-ip <public IPv4>
# debian:12-slim follows Debian 12 point releases, so a rebuild picks up security fixes
FROM debian:12-slim AS build
RUN apt-get update \
 && apt-get install -y --no-install-recommends g++ \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY deps/aoo/lib deps/aoo/lib
COPY deps/aoo/deps deps/aoo/deps
COPY server/relay server/relay
RUN sh server/relay/build.sh /src /out/canduet-server

FROM debian:12-slim
COPY --from=build /out/canduet-server /usr/local/bin/canduet-server
# nobody: the server needs no files and no privileges (ports are above 1024)
USER 65534:65534
EXPOSE 10998/tcp 10998/udp 40000-40999/udp
ENTRYPOINT ["/usr/local/bin/canduet-server"]
