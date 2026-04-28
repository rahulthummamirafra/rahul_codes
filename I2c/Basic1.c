#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/string.h>

#define OLED_ADDR 0x3C

/* Control bytes */
#define CMD  0x00
#define DATA 0x40

static struct i2c_client *client;

/* ---------- FONT (H E L O only) ---------- */
static const u8 font5x8[][5] = {
/* H */ {0x7F, 0x08, 0x08, 0x08, 0x7F},
/* E */ {0x7F, 0x49, 0x49, 0x49, 0x41},
/* L */ {0x7F, 0x40, 0x40, 0x40, 0x40},
/* O */ {0x3E, 0x41, 0x41, 0x41, 0x3E},
};

/* Map character */
static const u8* get_char(char c)
{
    switch (c) {
    case 'H': return font5x8[0];
    case 'E': return font5x8[1];
    case 'L': return font5x8[2];
    case 'O': return font5x8[3];
    default:  return font5x8[2];
    }
}

/* ---------- I2C SEND ---------- */
static int oled_cmd(u8 cmd)
{
    u8 buf[2] = {CMD, cmd};
    return i2c_master_send(client, buf, 2);
}

static int oled_data(u8 *data, int len)
{
    u8 buf[129];

    if (len > 128)
        len = 128;

    buf[0] = DATA;
    memcpy(&buf[1], data, len);

    return i2c_master_send(client, buf, len + 1);
}

/* ---------- OLED FUNCTIONS ---------- */
static void oled_init(void)
{
    u8 cmds[] = {
        0xAE,
        0xD5, 0x80,
        0xA8, 0x1F,
        0xD3, 0x00,
        0x40,
        0x8D, 0x14,
        0x20, 0x00,
        0xA1,
        0xC8,
        0xDA, 0x02,
        0x81, 0x8F,
        0xD9, 0xF1,
        0xDB, 0x40,
        0xA4,
        0xA6,
        0xAF
    };

    int i;
    for (i = 0; i < ARRAY_SIZE(cmds); i++)
        oled_cmd(cmds[i]);

    msleep(100);
}

static void oled_clear(void)
{
    u8 zero[128] = {0};
    int i;

    for (i = 0; i < 4; i++) {
        oled_cmd(0xB0 + i);
        oled_cmd(0x00);
        oled_cmd(0x10);
        oled_data(zero, 128);
    }
}

static void oled_set_cursor(u8 page, u8 col)
{
    oled_cmd(0xB0 + page);
    oled_cmd(0x00 + (col & 0x0F));
    oled_cmd(0x10 + (col >> 4));
}

static void oled_char(char c)
{
    const u8 *bitmap = get_char(c);
    u8 buf[6];
    int i;

    for (i = 0; i < 5; i++)
        buf[i] = bitmap[i];

    buf[5] = 0x00; // space

    oled_data(buf, 6);
}

static void oled_print(char *str)
{
    while (*str)
        oled_char(*str++);
}

/* ---------- DRIVER ---------- */
static int oled_probe(struct i2c_client *cl,
                      const struct i2c_device_id *id)
{
    pr_info("SSD1306 OLED probe\n");

    client = cl;

    oled_init();
    oled_clear();

    /* Display HELLO */
    oled_set_cursor(1, 20);
    oled_print("HELLO");

    return 0;
}

static void oled_remove(struct i2c_client *cl)
{
    oled_cmd(0xAE); // display OFF
    pr_info("OLED removed\n");
}

static const struct i2c_device_id oled_id[] = {
    { "ssd1306_oled", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, oled_id);

static struct i2c_driver oled_driver = {
    .driver = {
        .name = "ssd1306_oled",
    },
    .probe = oled_probe,
    .remove = oled_remove,
    .id_table = oled_id,
};

module_i2c_driver(oled_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Rahul");
MODULE_DESCRIPTION("SSD1306 128x32 OLED I2C Driver - HELLO Display");
