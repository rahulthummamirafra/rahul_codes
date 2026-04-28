/dts-v1/;
/plugin/;

/ {
    compatible = "brcm,bcm2711";

    fragment@0 {
        target = <&spi0>;

        __overlay__ {
            status = "okay";

            spidev@0 {
                status = "disabled";   // disable default spidev
            };

            myspi: mydevice@0 {
                compatible = "mycompany,my-spi-device";
                reg = <0>;   // CS0
                spi-max-frequency = <500000>;
            };
        };
    };
};
