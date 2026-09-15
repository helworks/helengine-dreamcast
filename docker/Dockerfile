FROM pcercuei/dreamcast-toolchain@sha256:504ed29fad5e5565579bf00235fc011fdde656004e50bde4aa24a93b59e2a7bb

ARG KOS_COMMIT=fcfa7d869471591ca1c777543261a7bfea7cb726
ARG KOS_REPOSITORY=https://github.com/KallistiOS/KallistiOS.git

ENV KOS_BASE=/opt/toolchains/dc/kos \
    KOS_CC_BASE=/opt/toolchains/dc/sh-elf \
    KOS_CC_PREFIX=sh-elf \
    KOS_CXX=/opt/toolchains/dc/kos/utils/build_wrappers/kos-c++ \
    KOS_HOST_CC=/usr/bin/gcc \
    KOS_HOST_CXX=/usr/bin/g++

# The compiler is supplied by the pinned upstream image. Recreate the KOS
# checkout from its exact commit and build the SDK libraries with the image's
# normal KOS environment. Keeping the source checkout in the image also makes
# the compiler/wrapper paths available to the runtime build container.
RUN set -eux; \
    rm -rf "${KOS_BASE}"; \
    mkdir -p "${KOS_BASE}"; \
    git init "${KOS_BASE}"; \
    git -C "${KOS_BASE}" remote add origin "${KOS_REPOSITORY}"; \
    git -C "${KOS_BASE}" fetch --depth 1 origin "${KOS_COMMIT}"; \
    git -C "${KOS_BASE}" checkout --detach "${KOS_COMMIT}"; \
    test "$(git -C "${KOS_BASE}" rev-parse HEAD)" = "${KOS_COMMIT}"

# Preserve the source template and install a deterministic environment file.
RUN set -eux; \
    cp "${KOS_BASE}/environ_base.sh" "${KOS_BASE}/environ.sh.template"; \
    printf '%s\n' \
      '#!/usr/bin/env bash' \
      'export KOS_BASE=/opt/toolchains/dc/kos' \
      'export KOS_PORTS=/opt/toolchains/dc/kos-ports' \
      'export KOS_ARCH=dreamcast' \
      'export KOS_SUBARCH=pristine' \
      'source "${KOS_BASE}/environ_base.sh"' \
      > "${KOS_BASE}/environ.sh"; \
    chmod 0555 "${KOS_BASE}/environ.sh"

RUN bash -lc 'set -ex; source "${KOS_BASE}/environ.sh"; make -C "${KOS_BASE}" -j8'

# Keep the compiled SDK separate from the source identity mount used by the
# application build. The runtime container mounts the validated checkout at
# /opt/toolchains/dc/kos read-only, so this image-local copy supplies the
# already-built libraries without mutating that checkout.
RUN set -eux; \
    cp -a "${KOS_BASE}" /opt/toolchains/dc/kos-built; \
    sed -i "s#${KOS_BASE}#/opt/toolchains/dc/kos-built#g" /opt/toolchains/dc/kos-built/environ.sh

LABEL org.helengine.dreamcast.kos-commit="${KOS_COMMIT}" \
      org.helengine.dreamcast.upstream-base="pcercuei/dreamcast-toolchain@sha256:504ed29fad5e5565579bf00235fc011fdde656004e50bde4aa24a93b59e2a7bb" \
      org.helengine.dreamcast.compiler-abi="sh-elf little-endian -m4-single"

WORKDIR /src
