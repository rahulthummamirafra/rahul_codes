#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/io.h>
#include "font8x8_basic.h"

/* --- Constants --- */
#define DRIVER_NAME "ili9225_dht"
#define CLASS_NAME  "ili_dht"
#define DEVICE_NAME "ili_dht_char"
#define DHT_GPIO_4  (4 + 512)
#define GPIO_17     (17 + 512)
#define TIMER_PHY   0xFE003000
#define TIMER_CLO   0x04
#define TIMER_SIZE  0x1C
#define MSEC        1000

/* --- Structures --- */
struct dht_data {
    int temperature;
    int humidity;
};

struct ili9225_dht_dev {
    struct spi_device *spi;
    struct gpio_desc *dc;
    struct gpio_desc *reset;
    struct dht_data sensor;
    int irq;
    struct workqueue_struct *wq;
    struct work_struct work;
    void __iomem *timer_base;
};

static struct ili9225_dht_dev *g_dev;

/* --- SPI & Display Functions --- */
static int ili_write16(struct ili9225_dht_dev *dev, u16 value) {
    u8 buf[2] = { value >> 8, value & 0xFF };
    return spi_write(dev->spi, buf, 2);
}

static int ili_write_reg(struct ili9225_dht_dev *dev, u16 reg, u16 data) {
    gpiod_set_value(dev->dc, 0);
    ili_write16(dev, reg);
    gpiod_set_value(dev->dc, 1);
    return ili_write16(dev, data);
}

static void ili_set_window(struct ili9225_dht_dev *dev, u16 x, u16 y) {
    gpiod_set_value(dev->dc, 0);
    ili_write16(dev, 0x0020); gpiod_set_value(dev->dc, 1); ili_write16(dev, x);
    gpiod_set_value(dev->dc, 0);
    ili_write16(dev, 0x0021); gpiod_set_value(dev->dc, 1); ili_write16(dev, y);
    gpiod_set_value(dev->dc, 0);
    ili_write16(dev, 0x0022); gpiod_set_value(dev->dc, 1);
}

static void ili_draw_pixel(struct ili9225_dht_dev *dev, int x, int y, u16 color) {
    ili_set_window(dev, x, y);
    ili_write16(dev, color);
}

static void ili_draw_char(struct ili9225_dht_dev *dev, int x, int y, char c, u16 color) {
    if(c < 32 || c > 127) return;
    const uint8_t *bitmap = font8x8[c - 32];
    for(int row = 0; row < 8; row++) {
        for(int col = 0; col < 8; col++) {
            if(bitmap[row] & (1 << (7 - col))) {
                // Draw 2x2 blocks for visibility
                for(int i=0; i<2; i++)
                    for(int j=0; j<2; j++)
                        ili_draw_pixel(dev, x + col*2 + i, y + row*2 + j, color);
            }
        }
    }
}

static void ili_draw_string(struct ili9225_dht_dev *dev, int x, int y, const char *str, u16 color) {
    while(*str) {
        ili_draw_char(dev, x, y, *str++, color);
        x += 16;
    }
}

static void ili_fill_screen(struct ili9225_dht_dev *dev, u16 color) {
    ili_write_reg(dev, 0x0036, 175); ili_write_reg(dev, 0x0037, 0);
    ili_write_reg(dev, 0x0038, 219); ili_write_reg(dev, 0x0039, 0);
    ili_set_window(dev, 0, 0);
    for (int i = 0; i < (176 * 220); i++) ili_write16(dev, color);
}

/* --- DHT11 Logic --- */
static void delay_us(unsigned int us) {
    u32 start = readl(g_dev->timer_base + TIMER_CLO);
    while ((readl(g_dev->timer_base + TIMER_CLO) - start) < us);
}

static void sensor_work_func(struct work_struct *work) {
    struct ili9225_dht_dev *dev = container_of(work, struct ili9225_dht_dev, work);
    u8 buf[5] = {0};
    char temp_str[32], hum_str[32];

    /* DHT11 Handshake */
    gpio_direction_output(DHT_GPIO_4, 0);
    delay_us(18 * MSEC);
    gpio_set_value(DHT_GPIO_4, 1);
    delay_us(30);
    gpio_direction_input(DHT_GPIO_4);

    while (gpio_get_value(DHT_GPIO_4)); // Wait for Response
    while (!gpio_get_value(DHT_GPIO_4));
    while (gpio_get_value(DHT_GPIO_4));

    /* Read Data */
    for (int j = 0; j < 5; j++) {
        for (int i = 0; i < 8; i++) {
            while (!gpio_get_value(DHT_GPIO_4));
            delay_us(35);
            if (gpio_get_value(DHT_GPIO_4)) buf[j] |= (1 << (7 - i));
            while (gpio_get_value(DHT_GPIO_4));
        }
    }

    if (((buf[0] + buf[1] + buf[2] + buf[3]) & 0xFF) == buf[4]) {
        dev->sensor.humidity = buf[0];
        dev->sensor.temperature = buf[2];
        
        /* Update Display */
        ili_fill_screen(dev, 0xFFFF); // Clear Screen White
        snprintf(temp_str, sizeof(temp_str), "Temp: %d C", dev->sensor.temperature);
        snprintf(hum_str, sizeof(hum_str), "Hum: %d %%", dev->sensor.humidity);
        
        ili_draw_string(dev, 20, 50, temp_str, 0xF800); // Red Text
        ili_draw_string(dev, 20, 100, hum_str, 0x001F); // Blue Text
    }

    enable_irq(dev->irq);
}

static irqreturn_t gpio_isr(int irq, void *dev_id) {
    disable_irq_nosync(irq);
    queue_work(g_dev->wq, &g_dev->work);
    return IRQ_HANDLED;
}

/* --- SPI Probe --- */
static int ili_dht_probe(struct spi_device *spi) {
    struct ili9225_dht_dev *dev;
    
    dev = devm_kzalloc(&spi->dev, sizeof(*dev), GFP_KERNEL);
    if (!dev) return -ENOMEM;
    g_dev = dev;
    dev->spi = spi;

    /* GPIO Setup */
    dev->dc = devm_gpiod_get(&spi->dev, "dc", GPIOD_OUT_LOW);
    dev->reset = devm_gpiod_get(&spi->dev, "reset", GPIOD_OUT_HIGH);
    gpio_request(DHT_GPIO_4, "dht_data");
    gpio_request(GPIO_17, "dht_irq_btn");
    gpio_direction_input(GPIO_17);

    /* Timer Setup */
    dev->timer_base = ioremap(TIMER_PHY, TIMER_SIZE);

    /* Interrupt & WQ Setup */
    dev->wq = create_singlethread_workqueue("dht_lcd_wq");
    INIT_WORK(&dev->work, sensor_work_func);
    dev->irq = gpio_to_irq(GPIO_17);
    request_irq(dev->irq, gpio_isr, IRQF_TRIGGER_RISING, "ili_dht_irq", NULL);

    /* SPI Setup */
    spi->mode = SPI_MODE_0;
    spi->bits_per_word = 8;
    spi_setup(spi);

    /* LCD Init */
    gpiod_set_value(dev->reset, 0); msleep(20); gpiod_set_value(dev->reset, 1); msleep(50);
    ili_write_reg(dev, 0x0001, 0x011C); ili_write_reg(dev, 0x0002, 0x0100);
    ili_write_reg(dev, 0x0003, 0x1030); ili_write_reg(dev, 0x0007, 0x1017);
    ili_fill_screen(dev, 0x0000);
    ili_draw_string(dev, 10, 80, "Press Button", 0xFFFF);

    return 0;
}

static void ili_dht_remove(struct spi_device *spi) {
    free_irq(g_dev->irq, NULL);
    destroy_workqueue(g_dev->wq);
    iounmap(g_dev->timer_base);
    gpio_free(DHT_GPIO_4);
    gpio_free(GPIO_17);
}

static const struct of_device_id ili_dht_ids[] = {
    { .compatible = "ilitek,ili9225" }, { }
};

static struct spi_driver ili_dht_driver = {
    .driver = { .name = DRIVER_NAME, .of_match_table = ili_dht_ids },
    .probe = ili_dht_probe, .remove = ili_dht_remove,
};

module_spi_driver(ili_dht_driver);
MODULE_LICENSE("GPL");
