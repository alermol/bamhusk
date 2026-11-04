ARG BASE=debian:trixie-slim
FROM ${BASE} AS build

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        bzip2 \
        ca-certificates \
        cmake \
        curl \
        ninja-build \
        pkg-config \
        zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .
RUN HTSLIB_PREFIX=/opt/htslib JOBS=4 ./scripts/build-static.sh

FROM scratch

COPY --from=build /src/dist/husk-*-linux-* /usr/local/bin/husk
ENTRYPOINT ["/usr/local/bin/husk"]
CMD ["--help"]
