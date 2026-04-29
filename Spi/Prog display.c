// ili9225_spi_driver.c

#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>

#define DRIVER_NAME "ili9225"
#define CLASS_NAME  "ili"
#define DEVICE_NAME "ili9225_char"

static dev_t dev_num;
static struct class *ili_class;
static struct cdev ili_cdev;

struct ili9225 {
    struct spi_device *spi;
    struct gpio_desc *dc;
    struct gpio_desc *reset;
};

static struct ili9225 *g_lcd;

/* ---------------- SPI WRITE ---------------- */
static int ili9225_write16(struct ili9225 *lcd, u16 value)
{
    u8 buf[2] = { value >> 8, value & 0xFF };
    int ret = spi_write(lcd->spi, buf, 2);

    if (ret < 0)
        pr_err("SPI write failed\n");

    return ret;
}

/* ---------------- REG WRITE ---------------- */
static void ili9225_write_reg(struct ili9225 *lcd, u16 reg, u16 data)
{
    gpiod_set_value(lcd->dc, 0);
    ili9225_write16(lcd, reg);

    gpiod_set_value(lcd->dc, 1);
    ili9225_write16(lcd, data);
}

/* ---------------- RESET ---------------- */
static void ili9225_reset(struct ili9225 *lcd)
{
    gpiod_set_value(lcd->reset, 1);
    msleep(5);
    gpiod_set_value(lcd->reset, 0);
    msleep(20);
    gpiod_set_value(lcd->reset, 1);
    msleep(50);
}

/* ---------------- INIT ---------------- */
static void ili9225_init(struct ili9225 *lcd)
{
    ili9225_reset(lcd);

    ili9225_write_reg(lcd, 0x01, 0x011C);
    ili9225_write_reg(lcd, 0x02, 0x0100);
    ili9225_write_reg(lcd, 0x03, 0x1030);
    ili9225_write_reg(lcd, 0x08, 0x0808);
    ili9225_write_reg(lcd, 0x0F, 0x0B01);

    ili9225_write_reg(lcd, 0x10, 0x0A00);
    ili9225_write_reg(lcd, 0x11, 0x1038);
    msleep(50);

    ili9225_write_reg(lcd, 0x12, 0x1121);
    ili9225_write_reg(lcd, 0x13, 0x0063);
    ili9225_write_reg(lcd, 0x14, 0x5A00);
    msleep(50);

    ili9225_write_reg(lcd, 0x07, 0x1017);
}

/* ---------------- FILL SCREEN ---------------- */
static void ili9225_fill(struct ili9225 *lcd, u16 color)
{
    int x, y;

    ili9225_write_reg(lcd, 0x36, 175);
    ili9225_write_reg(lcd, 0x37, 0);
    ili9225_write_reg(lcd, 0x38, 219);
    ili9225_write_reg(lcd, 0x39, 0);

    ili9225_write_reg(lcd, 0x20, 0);
    ili9225_write_reg(lcd, 0x21, 0);

    gpiod_set_value(lcd->dc, 0);
    ili9225_write16(lcd, 0x22);
    gpiod_set_value(lcd->dc, 1);

    for (y = 0; y < 220; y++)
        for (x = 0; x < 176; x++)
            ili9225_write16(lcd, color);
}

/* ---------------- WRITE FROM USER ---------------- */
static ssize_t ili_write(struct file *file,
                         const char __user *buf,
                         size_t len,
                         loff_t *off)
{
    char kbuf[128];

    if (!g_lcd) {
        pr_err("LCD not ready\n");
        return -ENODEV;
    }

    if (len > 127)
        len = 127;

    if (copy_from_user(kbuf, buf, len))
        return -EFAULT;

    kbuf[len] = '\0';

    pr_info("User wrote: %s\n", kbuf);

    // Just fill screen with color for test
    ili9225_fill(g_lcd, 0xF800);  // RED

    return len;
}

/* ---------------- FILE OPS ---------------- */
static struct file_operations fops = {
    .owner = THIS_MODULE,
    .write = ili_write,
};

/* ---------------- PROBE ---------------- */
static int ili9225_probe(struct spi_device *spi)
{
    struct ili9225 *lcd;
    int ret;

    pr_info("ILI9225 probe called\n");

    lcd = devm_kzalloc(&spi->dev, sizeof(*lcd), GFP_KERNEL);
    if (!lcd)
        return -ENOMEM;

    lcd->spi = spi;
    spi_set_drvdata(spi, lcd);

    lcd->dc = devm_gpiod_get(&spi->dev, "dc", GPIOD_OUT_LOW);
    if (IS_ERR(lcd->dc))
        return PTR_ERR(lcd->dc);

    lcd->reset = devm_gpiod_get(&spi->dev, "reset", GPIOD_OUT_HIGH);
    if (IS_ERR(lcd->reset))
        return PTR_ERR(lcd->reset);

    spi->mode = SPI_MODE_0;
    spi->bits_per_word = 8;
    spi_setup(spi);

    ili9225_init(lcd);
    ili9225_fill(lcd, 0xFFFF);

    g_lcd = lcd;

    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0)
        return ret;

    cdev_init(&ili_cdev, &fops);

    ret = cdev_add(&ili_cdev, dev_num, 1);
    if (ret < 0)
        return ret;

    ili_class = class_create(CLASS_NAME);
    device_create(ili_class, NULL, dev_num, NULL, DEVICE_NAME);

    pr_info("ILI9225 driver loaded\n");
    return 0;
}

/* ---------------- REMOVE ---------------- */
static void ili9225_remove(struct spi_device *spi)
{
    device_destroy(ili_class, dev_num);
    class_destroy(ili_class);
    cdev_del(&ili_cdev);
    unregister_chrdev_region(dev_num, 1);

    pr_info("ILI9225 removed\n");
}

/* ---------------- DTS MATCH ---------------- */
static const struct of_device_id ili9225_dt_ids[] = {
    { .compatible = "ilitek,ili9225" },
    { }
};
MODULE_DEVICE_TABLE(of, ili9225_dt_ids);

/* ---------------- DRIVER ---------------- */
static struct spi_driver ili9225_driver = {
    .driver = {
        .name = DRIVER_NAME,
        .of_match_table = ili9225_dt_ids,
    },
    .probe  = ili9225_probe,
    .remove = ili9225_remove,
};

module_spi_driver(ili9225_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("display");
MODULE_DESCRIPTION("ILI9225 SPI LCD Driver");
