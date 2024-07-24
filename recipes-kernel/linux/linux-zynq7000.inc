DESCRIPTION = "Linux Kernel for zynq-7000"
LICENSE = "GPLv2"

KERNEL_REPO ?= "git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git"
KERNEL_PROT ?= "https"

LINUX_VERSION ?= "5.4"
LINUX_VERSION_PREFIX ?= "linux-"
LINUX_VERSION_SUFFIX ?= ".y"
SRCREV ?= "${AUTOREV}"
KERNEL_VERSION_SANITY_SKIP="1"
PV = "${LINUX_VERSION}${LINUX_VERSION_SUFFIX}"
PV:append = "+git${SRCPV}"

KBRANCH ?= "${LINUX_VERSION_PREFIX}${LINUX_VERSION}${LINUX_VERSION_SUFFIX}"

SRC_URI = "${KERNEL_REPO};protocol=${KERNEL_PROT};branch=${KBRANCH} \
        file://0003-Drivers-for-xilinx-frame-buffer-copied-from-xlnx-lin.patch \
        file://0008-xilinx-framebuffer-kernel-configs.patch \
        file://0001-videodev2.h-copied-from-linux-xlnx.patch \
        file://0010-drm_fourcc.h-copied-from-linux-xlnx.patch \
        file://0001-export-of_chosen-symbol.patch \
        file://0001-Select-FB_CFB_IMAGEBLIT-FB_CFB_COPYAREA-FB_CFB_FILLR.patch"

require recipes-kernel/linux/linux-yocto.inc

FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"
SRC_URI += "file://zynq-defconfig"
