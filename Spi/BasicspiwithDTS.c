#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/of.h>

#define DRIVER_NAME "my_spi_driver"

/* Probe */
static int my_spi_probe(struct spi_device *spi)
{
    int ret;
    u8 tx_buf[] = "Hello SPI";
    u8 rx_buf[10] = {0};

    dev_info(&spi->dev, "SPI device probed\n");

    /* Configure SPI */
    spi->mode = SPI_MODE_3;          // CPOL=1, CPHA=1
    spi->bits_per_word = 8;
    spi->max_speed_hz = 500000;

    ret = spi_setup(spi);
    if (ret) {
        dev_err(&spi->dev, "SPI setup failed\n");
        return ret;
    }

    /* Send data */
    ret = spi_write(spi, tx_buf, sizeof(tx_buf));
    if (ret < 0) {
        dev_err(&spi->dev, "SPI write failed\n");
        return ret;
    }

    dev_info(&spi->dev, "Data sent: %s\n", tx_buf);

    /* Receive data */
    ret = spi_read(spi, rx_buf, sizeof(rx_buf));
    if (ret < 0) {
        dev_err(&spi->dev, "SPI read failed\n");
        return ret;
    }

    dev_info(&spi->dev, "Data received: %s\n", rx_buf);

    return 0;
}

/* Remove */
static void my_spi_remove(struct spi_device *spi)
{
    dev_info(&spi->dev, "SPI device removed\n");
}

/* Device Tree match */
static const struct of_device_id my_spi_dt_ids[] = {
    { .compatible = "mycompany,my-spi-device" },
    { }
};
MODULE_DEVICE_TABLE(of, my_spi_dt_ids);

/* SPI driver */
static struct spi_driver my_spi_driver = {
    .driver = {
        .name = DRIVER_NAME,
        .of_match_table = my_spi_dt_ids,
    },
    .probe = my_spi_probe,
    .remove = my_spi_remove,
};

module_spi_driver(my_spi_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Rahul");
MODULE_DESCRIPTION("Proper SPI Driver using Linux SPI Framework");


