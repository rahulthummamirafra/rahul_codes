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

/* -------- BCD helpers -------- */
static int bcd_to_dec(u8 val)
{
    return ((val >> 4) * 10) + (val & 0x0F);
}

static u8 dec_to_bcd(int val)
{
    return ((val / 10) << 4) | (val % 10);
}

/* -------- I2C helpers -------- */
static int ds3231_read_reg(struct i2c_client *client, u8 reg)
{
    int ret = i2c_smbus_read_byte_data(client, reg);
    if (ret < 0)
        dev_err(&client->dev, "Read fail reg=0x%02x\n", reg);
    return ret;
}

static int ds3231_write_reg(struct i2c_client *client, u8 reg, u8 val)
{
    int ret = i2c_smbus_write_byte_data(client, reg, val);
    if (ret < 0)
        dev_err(&client->dev, "Write fail reg=0x%02x\n", reg);
    return ret;
}

/* -------- Read time (fixed) -------- */
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

    if (sec < 0 || min < 0 || hour < 0)
        return;

    /* Clear CH bit (bit7 of seconds) */
    sec &= 0x7F;

    /* Handle 12/24 hr format */
    if (hour & 0x40) {  // 12-hour mode
        hour = bcd_to_dec(hour & 0x1F);
        if (hour & 0x20) // PM bit
            hour += 12;
    } else {
        hour = bcd_to_dec(hour & 0x3F);
    }

    sec  = bcd_to_dec(sec);
    min  = bcd_to_dec(min);
    day  = bcd_to_dec(day);
    date = bcd_to_dec(date);
    mon  = bcd_to_dec(mon);
    year = bcd_to_dec(year);

    dev_info(&client->dev,
        "Time: %02d:%02d:%02d Date: %02d/%02d/20%02d Day:%s\n",
        hour, min, sec, date, mon, year, days[day-1]);
}

/* -------- Set time (fixed) -------- */
static void ds3231_set_time(struct i2c_client *client)
{
    /* Clear CH bit → ensure clock runs */
    ds3231_write_reg(client, DS3231_SEC,  dec_to_bcd(0) & 0x7F);
    ds3231_write_reg(client, DS3231_MIN,  dec_to_bcd(30));
    ds3231_write_reg(client, DS3231_HOUR, dec_to_bcd(12)); // 24hr mode
    ds3231_write_reg(client, DS3231_DATE, dec_to_bcd(16));
    ds3231_write_reg(client, DS3231_MON,  dec_to_bcd(2));
    ds3231_write_reg(client, DS3231_YEAR, dec_to_bcd(26));
    ds3231_write_reg(client, DS3231_DAY,  dec_to_bcd(2));

    dev_info(&client->dev, "RTC time set\n");
}

/* -------- Probe -------- */
static int ds3231_probe(struct i2c_client *client)
{
    dev_info(&client->dev, "DS3231 detected\n");

    ds3231_set_time(client);
    ds3231_read_time(client);

    return 0;
}

static void ds3231_remove(struct i2c_client *client)
{
    dev_info(&client->dev, "DS3231 removed\n");
}

/* -------- Device Tree -------- */
static const struct of_device_id ds3231_dt_ids[] = {
    { .compatible = "maxim,ds3231" },
    { }
};
MODULE_DEVICE_TABLE(of, ds3231_dt_ids);

/* -------- Driver -------- */
static struct i2c_driver ds3231_driver = {
    .driver = {
        .name = "ds3231_driver",
        .of_match_table = ds3231_dt_ids,
    },
    .probe  = ds3231_probe,
    .remove = ds3231_remove,
};

module_i2c_driver(ds3231_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Stable DS3231 Driver");
