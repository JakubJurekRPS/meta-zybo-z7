require conf/machine/include/arm/armv7a/tune-cortexa9.inc

KERNEL_PROVIDER ?= "linux-zynq"
PREFERRED_PROVIDER_virtual/kernel ?= "${KERNEL_PROVIDER}"
# PREFERRED_PROVIDER_virtual/bootloader = "u-boot"
# Increase this everytime you change something in the kernel
MACHINE_KERNEL_PR = "r1"

KERNEL_IMAGETYPE = "zImage"

MACHINE_EXTRA_RRECOMMENDS = " kernel-modules"
MACHINE_ESSENTIAL_EXTRA_RDEPENDS = " kernel-module-jj-xlnx-frmbuf"
MACHINE_FEATURES = "kernel26"

# file system images required
IMAGE_FSTYPES ?= "tar.gz"
