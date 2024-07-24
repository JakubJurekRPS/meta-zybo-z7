FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"
SRC_URI:append = " file://system-top.dts \
                file://zynq-7000.dtsi \
                file://pl.dtsi \
                file://pcw.dtsi"

do_configure:append(){
    cp ${ZYNQ_DTS_DIR}/system-top.dts ${S}/arch/arm/boot/dts
    cp ${ZYNQ_DTS_DIR}/zynq-7000.dtsi ${S}/arch/arm/boot/dts
    cp ${ZYNQ_DTS_DIR}/pl.dtsi ${S}/arch/arm/boot/dts
    cp ${ZYNQ_DTS_DIR}/pcw.dtsi ${S}/arch/arm/boot/dts
    echo "dtb-$(CONFIG_ARCH_ZYNQ) += system-top.dtb" >> ${S}/arch/arm/boot/dts/Makefile
}