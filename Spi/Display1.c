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
#include "font8x8_basic.h"
#define DRIVER_NAME "ili9225"
#define CLASS_NAME  "ili"
#define DEVICE_NAME "ili9225_char"

static dev_t dev_num;
static struct class *ili_class;
static struct cdev ili_cdev;

static struct ili9225 *g_lcd;


struct ili9225 {
    struct spi_device *spi;
    struct gpio_desc *dc;
    struct gpio_desc *reset;
};

/* ---------------------------------------------------- */
/* SPI 16-bit write                                     */
/* ---------------------------------------------------- */
static int ili9225_write16(struct ili9225 *lcd, u16 value)
{
    u8 buf[2];
    buf[0] = value >> 8;
    buf[1] = value & 0xFF;

    return spi_write(lcd->spi, buf, 2);
}

/* ---------------------------------------------------- */
/* Register write                                        */
/* ---------------------------------------------------- */
static int ili9225_write_reg(struct ili9225 *lcd, u16 reg, u16 data)
{
    gpiod_set_value(lcd->dc, 0);
    ili9225_write16(lcd, reg);

    gpiod_set_value(lcd->dc, 1);
    return ili9225_write16(lcd, data);
}

/* ---------------------------------------------------- */
/* Reset                                                 */
/* ---------------------------------------------------- */
static void ili9225_reset(struct ili9225 *lcd)
{
    gpiod_set_value(lcd->reset, 1);
    msleep(5);
    gpiod_set_value(lcd->reset, 0);
    msleep(20);
    gpiod_set_value(lcd->reset, 1);
    msleep(50);
}

/* ---------------------------------------------------- */
/* Init sequence                                         */
/* ---------------------------------------------------- */
static void ili9225_init(struct ili9225 *lcd)
{
    ili9225_reset(lcd);

    ili9225_write_reg(lcd, 0x0001, 0x011C);
    ili9225_write_reg(lcd, 0x0002, 0x0100);
    ili9225_write_reg(lcd, 0x0003, 0x1030);
    ili9225_write_reg(lcd, 0x0008, 0x0808);
    ili9225_write_reg(lcd, 0x000C, 0x0000);
    ili9225_write_reg(lcd, 0x000F, 0x0B01);

    ili9225_write_reg(lcd, 0x0010, 0x0A00);
    ili9225_write_reg(lcd, 0x0011, 0x1038);
    msleep(50);
    ili9225_write_reg(lcd, 0x0012, 0x1121);
    ili9225_write_reg(lcd, 0x0013, 0x0063);
    ili9225_write_reg(lcd, 0x0014, 0x5A00);
    msleep(50);

    ili9225_write_reg(lcd, 0x0007, 0x1017);
    msleep(20);
}

/* ---------------------------------------------------- */
/* Fill screen                                           */
/* ---------------------------------------------------- */
static void ili9225_fill(struct ili9225 *lcd, u16 color)
{
    int x, y;

    ili9225_write_reg(lcd, 0x0036, 175);
    ili9225_write_reg(lcd, 0x0037, 0);
    ili9225_write_reg(lcd, 0x0038, 219);
    ili9225_write_reg(lcd, 0x0039, 0);
    ili9225_write_reg(lcd, 0x0020, 0);
    ili9225_write_reg(lcd, 0x0021, 0);

    gpiod_set_value(lcd->dc, 0);
    ili9225_write16(lcd, 0x0022);
    gpiod_set_value(lcd->dc, 1);

    for (y = 0; y < 220; y++)
        for (x = 0; x < 176; x++)
            ili9225_write16(lcd, color);
}

static void drawPixel(int x, int y, uint16_t color)
{
    struct ili9225 *lcd = g_lcd;

    // Set X
    gpiod_set_value(lcd->dc, 0);
    ili9225_write16(lcd, 0x0020);
    gpiod_set_value(lcd->dc, 1);
    ili9225_write16(lcd, x);

    // Set Y
    gpiod_set_value(lcd->dc, 0);
    ili9225_write16(lcd, 0x0021);
    gpiod_set_value(lcd->dc, 1);
    ili9225_write16(lcd, y);

    // Write pixel color
    gpiod_set_value(lcd->dc, 0);
    ili9225_write16(lcd, 0x0022);
    gpiod_set_value(lcd->dc, 1);
    ili9225_write16(lcd, color);
}

void drawChar(int x, int y, char c, uint16_t color)
{
    if(c < 32 || c > 127) return; // ignore non-printable
    const uint8_t *bitmap = font8x8[c - 32];

    for(int row = 0; row < 8; row++)
    {
        for(int col = 0; col < 8; col++)
        {
            if(bitmap[row] & (1 << (7 - col))) // bit 7 = leftmost
            {
                // draw 2x2 block instead of 1 pixel
                drawPixel(x + col*2, y + row*2, color);
                drawPixel(x + col*2+1, y + row*2, color);
                drawPixel(x + col*2, y + row*2+1, color);
                drawPixel(x + col*2+1, y + row*2+1, color);
            }
        }
    }
}

void drawString(int x,int y,const char *str,uint16_t color)
{
	for(int i=0;str[i];i++)
	{
		drawChar(y,x,str[i],0xf800);
		x+=16;
	}
}

/*
static ssize_t ili_write(struct file *file,
                         const char __user *buf,
                         size_t len,
                         loff_t *off)
{
    char kbuf[128];

    if (len > 127)
        len = 127;

    if (copy_from_user(kbuf, buf, len))
        return -EFAULT;

    kbuf[len] = '\0';

    drawString(10, 20, kbuf, 0x0000);  // draw text

    return len;
}
*/

static struct file_operations fops = {
    .owner = THIS_MODULE,
  //  .write = ili_write,
};
/* ---------------------------------------------------- */
/* Probe                                                 */
/* ---------------------------------------------------- */
static int ili9225_probe(struct spi_device *spi)
{
    struct ili9225 *lcd;

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
    ili9225_fill(lcd, 0xFFFF);  // white screen

    dev_info(&spi->dev, "ILI9225 LCD initialized\n");

g_lcd = lcd;

alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);

cdev_init(&ili_cdev, &fops);
cdev_add(&ili_cdev, dev_num, 1);

ili_class = class_create(CLASS_NAME);
device_create(ili_class, NULL, dev_num, NULL, DEVICE_NAME);

    drawString(10, 20, "HI....", 0x0000);  // draw text
    return 0;
}

/* ---------------------------------------------------- */
/* Remove                                                */
/* ---------------------------------------------------- */

/* ---------------------------------------------------- */
/* Device Tree Match                                     */
/* ---------------------------------------------------- */
static void ili9225_remove(struct spi_device *spi)
{
    device_destroy(ili_class, dev_num);
    class_destroy(ili_class);
    cdev_del(&ili_cdev);
    unregister_chrdev_region(dev_num, 1);

    dev_info(&spi->dev, "ILI9225 removed\n");
}



static const struct of_device_id ili9225_dt_ids[] = {
    { .compatible = "ilitek,ili9225" },
    { }
};
MODULE_DEVICE_TABLE(of, ili9225_dt_ids);

/* ---------------------------------------------------- */
/* SPI Driver Struct                                     */
/* ---------------------------------------------------- */
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
MODULE_AUTHOR(“display");
MODULE_DESCRIPTION("ILI9225 SPI LCD Driver");
