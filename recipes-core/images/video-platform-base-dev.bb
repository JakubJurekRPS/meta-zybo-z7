SUMMARY = "Basic video platform development image"

inherit core-image

IMAGE_FEATURES:append = " tools-debug ssh-server-openssh debug-tweaks dbg-pkgs dev-pkgs"
IMAGE_FEATURES:remove = " splash"

DISTRO_FEATURES:remove = " alsa  bluetooth  wifi  3g  nfc  x11  wayland  vulkan  pulseaudio"
DISTRO_FEATURES:append = " opengl"

CORE_IMAGE_EXTRA_INSTALL:append = " pstree strace"

CORE_IMAGE_EXTRA_INSTALL:append = " qtbase qtdeclarative qttools \
    qtquickcontrols2-qmlplugins qtquickcontrols2 \
    qtquickcontrols qtquickcontrols-qmlplugins \
    qtserialport rsync openssh-sftp-server liberation-fonts"

PACKAGECONFIG_DISTRO:pn-qtbase = " linuxfb eglfs gles2"
PACKAGECONFIG_FONTS = " fontconfig"

# Somehow it is not installed by default - something wrong with meta-qt5 probably 
# (It was installed by default in the past)
TOOLCHAIN_TARGET_TASK:append = " libgles3-mesa-dev"
# CORE_IMAGE_EXTRA_INSTALL:append = " libgles3-mesa-dev"