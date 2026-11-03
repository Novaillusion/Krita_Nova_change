# Krita Android build environment
#
# Mirrors KDE's CI image "krita-android-sdk36-2204" (Ubuntu 22.04 + Android SDK 36
# + NDK r27d) closely enough to run Krita's own CI scripts locally.
#
#   docker build -t krita-android-env packaging/android/local-build

FROM ubuntu:22.04

ARG DEBIAN_FRONTEND=noninteractive

# --- Host toolchain ---------------------------------------------------------
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential git ca-certificates curl wget unzip zip rsync file \
        python3 python3-venv python3-pip python3-dev \
        ninja-build ccache pkg-config patchelf \
        autoconf automake libtool gettext gperf bison flex nasm \
        openjdk-17-jdk-headless \
        qtdeclarative5-dev-tools \
    && rm -rf /var/lib/apt/lists/*

# Newer CMake than Ubuntu 22.04 ships (3.22). Pinned below 4.0 on purpose:
# CMake 4 drops compatibility with old cmake_minimum_required() calls.
RUN pip3 install --no-cache-dir "cmake>=3.28,<4"

# --- Android SDK / NDK ------------------------------------------------------
# Versions taken from packaging/android/apk/build.gradle:
#   buildToolsVersion 36.0.0, targetSdk 36, ndkVersion 27.3.13750724
ENV ANDROID_HOME=/opt/android-tooling/sdk
ENV ANDROID_SDK_ROOT=${ANDROID_HOME}
ENV ANDROID_NDK_VERSION=27.3.13750724
ENV ANDROID_NDK_ROOT=${ANDROID_HOME}/ndk/${ANDROID_NDK_VERSION}
ENV JAVA_HOME=/usr/lib/jvm/java-17-openjdk-amd64

RUN mkdir -p ${ANDROID_HOME}/cmdline-tools \
    && curl -fsSL -o /tmp/clt.zip \
       https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip \
    && unzip -q /tmp/clt.zip -d ${ANDROID_HOME}/cmdline-tools \
    && mv ${ANDROID_HOME}/cmdline-tools/cmdline-tools ${ANDROID_HOME}/cmdline-tools/latest \
    && rm /tmp/clt.zip

ENV PATH=${ANDROID_HOME}/cmdline-tools/latest/bin:${ANDROID_HOME}/platform-tools:${PATH}

RUN yes | sdkmanager --licenses > /dev/null \
    && sdkmanager --install \
        "platform-tools" \
        "platforms;android-36" \
        "platforms;android-33" \
        "build-tools;36.0.0" \
        "ndk;${ANDROID_NDK_VERSION}"

# Variables Krita's CI scripts and toolchain file expect
ENV KDECI_ANDROID_SDK_ROOT=${ANDROID_HOME}
ENV KDECI_ANDROID_NDK_ROOT=${ANDROID_NDK_ROOT}

RUN git config --system --add safe.directory '*' \
    && git config --system user.name "krita-local-build" \
    && git config --system user.email "krita-local-build@localhost"

COPY build-krita-android.sh /usr/local/bin/build-krita-android.sh
RUN chmod +x /usr/local/bin/build-krita-android.sh

WORKDIR /work
ENTRYPOINT ["/usr/local/bin/build-krita-android.sh"]
