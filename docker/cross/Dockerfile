# syntax=docker/dockerfile:1@sha256:ecfaec9ed6d810b56388c508f4121597bfbba70d41a6dfeee4d8cad5f295fc32

# Minimal Phase 1 cross-build runner for the standalone low-level middleware.
# ROS 2/colcon are intentionally excluded: ROS integration is a later project
# phase and must not be part of the core CMake cross-toolchain bootstrap.
ARG UBUNTU_IMAGE=ubuntu:22.04@sha256:3b06811b2afd352be909dd088a004166d665dc76d38b13eada33522a9d915c6f

FROM ${UBUNTU_IMAGE}

ARG UBUNTU_SNAPSHOT=20260814T000000Z
ARG CA_CERTIFICATES_VERSION=20260601~22.04.1
ARG RECIPE_SHA256=unknown
ARG PACKAGE_LOCK_SHA256=unknown
# USERNAME is a non-secret account name used only for host UID/GID mapping.
# hadolint ignore=DL3064
ARG USERNAME=builder
ARG USER_UID=1000
ARG USER_GID=${USER_UID}

ENV DEBIAN_FRONTEND=noninteractive
ENV LANG=C.UTF-8
ENV LC_ALL=C.UTF-8

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

LABEL org.opencontainers.image.title="robot-control RK3588 cross-build runner" \
      org.opencontainers.image.description="Minimal Ubuntu 22.04 amd64 CMake/GCC aarch64 cross-build environment" \
      org.opencontainers.image.version="phase1-2026-08-14" \
      robot-control.ubuntu-snapshot="${UBUNTU_SNAPSHOT}" \
      robot-control.recipe-sha256="${RECIPE_SHA256}" \
      robot-control.package-lock-sha256="${PACKAGE_LOCK_SHA256}"

RUN cat > /etc/apt/sources.list <<EOF
deb [check-valid-until=no] https://snapshot.ubuntu.com/ubuntu/${UBUNTU_SNAPSHOT}/ jammy main restricted universe multiverse
deb [check-valid-until=no] https://snapshot.ubuntu.com/ubuntu/${UBUNTU_SNAPSHOT}/ jammy-updates main restricted universe multiverse
deb [check-valid-until=no] https://snapshot.ubuntu.com/ubuntu/${UBUNTU_SNAPSHOT}/ jammy-backports main restricted universe multiverse
deb [check-valid-until=no] https://snapshot.ubuntu.com/ubuntu/${UBUNTU_SNAPSHOT}/ jammy-security main restricted universe multiverse
EOF

# The Ubuntu base image intentionally omits the CA bundle. APT still validates
# the signed Ubuntu InRelease metadata and package hashes during this one-time
# bootstrap; normal TLS verification is used for every subsequent operation.
RUN apt-get -o Acquire::https::Verify-Peer=false update && \
    apt-get -o Acquire::https::Verify-Peer=false install -y \
      --no-install-recommends \
      "ca-certificates=${CA_CERTIFICATES_VERSION}" && \
    rm -rf /var/lib/apt/lists/*

# The complete resolved dpkg set is verified against packages.lock below.
# hadolint ignore=DL3008
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        binutils-aarch64-linux-gnu \
        build-essential \
        ca-certificates \
        can-utils \
        ccache \
        clang \
        clang-format \
        clang-tidy \
        cmake \
        file \
        g++-aarch64-linux-gnu \
        gcc-aarch64-linux-gnu \
        gdb \
        gdb-multiarch \
        git \
        jq \
        ninja-build \
        pkg-config \
        python3 \
        shellcheck \
        strace \
        xz-utils && \
    rm -rf /var/lib/apt/lists/*

COPY docker/cross/packages.lock /usr/local/share/robot-control/packages.lock

RUN dpkg-query -W -f='${binary:Package}\t${Version}\t${Architecture}\n' \
      | LC_ALL=C sort > /tmp/packages.lock && \
    diff -u /usr/local/share/robot-control/packages.lock /tmp/packages.lock && \
    rm /tmp/packages.lock

RUN groupadd --gid "${USER_GID}" "${USERNAME}" && \
    useradd \
        --uid "${USER_UID}" \
        --gid "${USER_GID}" \
        --create-home \
        --shell /bin/bash \
        "${USERNAME}" && \
    mkdir -p /workspace "/home/${USERNAME}/.cache/ccache" && \
    chown -R "${USERNAME}:${USERNAME}" /workspace "/home/${USERNAME}"

USER ${USERNAME}

ENV HOME=/home/${USERNAME}
ENV CCACHE_DIR=/home/${USERNAME}/.cache/ccache
ENV CCACHE_MAXSIZE=5G

RUN ccache --max-size "${CCACHE_MAXSIZE}"

WORKDIR /workspace

CMD ["sleep", "infinity"]
