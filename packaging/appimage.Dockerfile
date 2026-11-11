# Ambiente do build portátil (AppImage). Ubuntu LTS de propósito: um AppImage
# só roda onde a glibc é pelo menos tão nova quanto a de onde foi gerado, e o
# 24.04 é a base do Linux Mint 22.
#
# O Qt não é o do Ubuntu: o 6.4 de lá não compila a QXmpp atual. Vem o build
# oficial da Qt (aqtinstall), que exige só glibc 2.28, e a QXmpp é compilada
# contra ele. Fica numa imagem para isso ser pago uma vez, não a cada
# `bin/appimage`.
FROM ubuntu:24.04

ARG QT_VERSION=6.10.3
ARG QXMPP_VERSION=v1.17.2

ENV DEBIAN_FRONTEND=noninteractive LANG=C.UTF-8

# Além das ferramentas, as bibliotecas que o Qt carrega em tempo de execução
# (xcb, xkbcommon, fontconfig, dbus): o linuxdeploy só leva para dentro do
# AppImage o que encontra aqui.
RUN apt-get update -qq && apt-get install -y -qq --no-install-recommends \
        build-essential g++-14 pkg-config git curl ca-certificates file \
        cmake ninja-build python3 python3-venv desktop-file-utils \
        libgl-dev libegl-dev libfontconfig1 libfreetype6 libdbus-1-3 \
        libxkbcommon0 libxkbcommon-x11-0 libxcb-cursor0 libxcb-icccm4 \
        libxcb-image0 libxcb-keysyms1 libxcb-randr0 libxcb-render-util0 \
        libxcb-shape0 libxcb-xinerama0 libxcb-xkb1 libx11-xcb1 \
        libwayland-dev wayland-protocols libssl3 \
    && rm -rf /var/lib/apt/lists/*

# O GCC 13, padrão do 24.04, para com um erro interno compilando a QXmpp. O
# 14 é do próprio Ubuntu, e a libstdc++ que ele exige já é a que o 24.04 traz.
ENV CC=gcc-14 CXX=g++-14

# qtimageformats: WebP, que os smileys usam.
RUN python3 -m venv /opt/aqt \
    && /opt/aqt/bin/pip install -q aqtinstall \
    && /opt/aqt/bin/aqt install-qt -O /opt/qt linux desktop "$QT_VERSION" linux_gcc_64 \
        -m qtimageformats \
    && rm -rf /opt/aqt
ENV QTDIR=/opt/qt/${QT_VERSION}/gcc_64

# Plugins que o Omajim não usa e que ligam bibliotecas ausentes aqui (libtiff
# 5, GTK 3, CUPS, clientes de bancos): o linuxdeploy tentaria levá-los e
# pararia. Do SQL só fica o SQLite, o do histórico; o tema GTK o próprio
# Omajim desliga (QT_QPA_PLATFORMTHEME=generic).
RUN cd "$QTDIR/plugins" \
    && rm -f imageformats/libqtiff.so platformthemes/libqgtk3.so \
        printsupport/libcupsprintersupport.so \
    && find sqldrivers -name '*.so' ! -name libqsqlite.so -delete
ENV PATH=${QTDIR}/bin:${PATH}

# Instalada em /opt/qxmpp/usr, o formato que o qxmpp.pri espera de QXMPP_ROOT.
RUN git clone -q --depth 1 --branch "$QXMPP_VERSION" \
        https://invent.kde.org/libraries/qxmpp.git /tmp/qxmpp \
    && cmake -S /tmp/qxmpp -B /tmp/qxmpp/build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QTDIR" \
        -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=lib \
        -DQT_VERSION_MAJOR=6 -DBUILD_TESTS=OFF -DBUILD_EXAMPLES=OFF \
        -DBUILD_DOCUMENTATION=OFF -DBUILD_OMEMO=OFF \
    && cmake --build /tmp/qxmpp/build \
    && DESTDIR=/opt/qxmpp cmake --install /tmp/qxmpp/build \
    && rm -rf /tmp/qxmpp
ENV QXMPP_ROOT=/opt/qxmpp
ENV LD_LIBRARY_PATH=/opt/qxmpp/usr/lib
