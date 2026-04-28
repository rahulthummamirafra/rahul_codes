#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/of.h>

/* DS3231 Registers */
#define DS3231_SEC   0x00
#define DS3231_MIN   0x01
#define DS3231_HOUR  0x02
#define DS3231_DAY   0x03
#define DS3231_DATE  0x04
#define DS3231_MON   0x05
#define DS3231_YEAR  0x06

static const char *days[] = {
    "SUN","MON","TUE","WED","THU","FRI","SAT"
};

/* BCD <-> DEC */
static u8 dec_to_bcd(int val)
{
    return ((val / 10) << 4) | (val % 10);
}

static int bcd_to_dec(u8 val)
{
    return ((val >> 4) * 10) + (val & 0x0F);
}

/* Read register */
static int ds3231_read_reg(struct i2c_client *client, u8 reg)
{
    return i2c_smbus_read_byte_data(client, reg);
}

/* Write register */
static int ds3231_write_reg(struct i2c_client *client, u8 reg, u8 val)
{
    int ret = i2c_smbus_write_byte_data(client, reg, val);
    if (ret < 0) {
        dev_err(&client->dev, "Write failed: reg=0x%02x\n", reg);
        return ret;
    }
    return 0;
}

/* Set time */
static int ds3231_set_time(struct i2c_client *client,
        int hour, int min, int sec,
        int date, int mon, int year, int day)
{
    int ret;

    ret = ds3231_write_reg(client, DS3231_SEC,  dec_to_bcd(sec));
    if (ret) return ret;

    ret = ds3231_write_reg(client, DS3231_MIN,  dec_to_bcd(min));
    if (ret) return ret;

    ret = ds3231_write_reg(client, DS3231_HOUR, dec_to_bcd(hour)); // 24hr mode
    if (ret) return ret;

    ret = ds3231_write_reg(client, DS3231_DAY,  dec_to_bcd(day));
    if (ret) return ret;

    ret = ds3231_write_reg(client, DS3231_DATE, dec_to_bcd(date));
    if (ret) return ret;

    ret = ds3231_write_reg(client, DS3231_MON,  dec_to_bcd(mon));
    if (ret) return ret;

    ret = ds3231_write_reg(client, DS3231_YEAR, dec_to_bcd(year));
    if (ret) return ret;

    dev_info(&client->dev, "Time set successfully\n");
    return 0;
}

/* Read time */
static void ds3231_read_time(struct i2c_client *client)
{
    int sec, min, hour, day, date, mon, year;

    sec  = ds3231_read_reg(client, DS3231_SEC);
    min  = ds3231_read_reg(client, DS3231_MIN);
    hour = ds3231_read_reg(client, DS3231_HOUR);
    day  = ds3231_read_reg(client, DS3231_DAY);
    date = ds3231_read_reg(client, DS3231_DATE);
    mon  = ds3231_read_reg(client, DS3231_MON);
    year = ds3231_read_reg(client, DS3231_YEAR);

    if (sec < 0 || min < 0 || hour < 0 ||
        day < 0 || date < 0 || mon < 0 || year < 0) {
        dev_err(&client->dev, "Read failed\n");
        return;
    }

    sec  = bcd_to_dec(sec & 0x7F);
    min  = bcd_to_dec(min & 0x7F);
    hour = bcd_to_dec(hour & 0x3F);  // 24hr format
    day  = bcd_to_dec(day & 0x07);
    date = bcd_to_dec(date & 0x3F);
    mon  = bcd_to_dec(mon & 0x1F);
    year = bcd_to_dec(year);

    if (day < 1 || day > 7)
        day = 1;

    dev_info(&client->dev,
        "Time: %02d:%02d:%02d  Date: %02d/%02d/20%02d  Day:%s\n",
        hour, min, sec, date, mon, year, days[day-1]);
}

/* Probe */
static int ds3231_probe(struct i2c_client *client)
{
    dev_info(&client->dev, "DS3231 detected\n");

    /* Example: Set time */
    ds3231_set_time(client, 11, 30, 0, 28, 4, 26, 2);

    /* Read time */
    ds3231_read_time(client);

    return 0;
}

/* Remove */
static void ds3231_remove(struct i2c_client *client)
{
    dev_info(&client->dev, "DS3231 removed\n");
}

/* DT match */
static const struct of_device_id ds3231_dt_ids[] = {
    { .compatible = "maxim,ds3231" },
    { }
};
MODULE_DEVICE_TABLE(of, ds3231_dt_ids);

/* I2C driver */
static struct i2c_driver ds3231_driver = {
    .driver = {
        .name = "ds3231_driver",
        .of_match_table = ds3231_dt_ids,
    },
    .probe = ds3231_probe,
    .remove = ds3231_remove,
};

module_i2c_driver(ds3231_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Rahul");
MODULE_DESCRIPTION("DS3231 RTC I2C Driver (Improved)");
