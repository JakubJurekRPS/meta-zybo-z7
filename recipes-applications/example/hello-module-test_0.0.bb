DESCRIPTION = "test hello module"
PRIORITY = "optional"
SECTION = "applications"

LICENSE = "GPLv2"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-2.0-only;md5=801f80980d171dd6425610833a22dbe6"

SRC_URI = "file://ioctl_test.c"

S = "${WORKDIR}"

do_compile() {
    ${CC} ${CFLAGS} -g3 ${LDFLAGS} ioctl_test.c -o hello-test
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 hello-test ${D}${bindir}
}