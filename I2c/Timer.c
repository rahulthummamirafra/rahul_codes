#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/delay.h>

/* DS3231 Registers */
#define DS3231_SEC   0x00
#define DS3231_MIN   0x01
#define DS3231_HOUR  0x02
#define DS3231_DAY   0x03
#define DS3231_DATE  0x04
#define DS3231_MON   0x05
#define DS3231_YEAR  0x06

static const char* days[]={"SUN","MON","TUE","WED","THU","FRI","SAT"};
/* BCD to Decimal */
static int bcd_to_dec(u8 val)
{
    return ((val >> 4) * 10) + (val & 0x0F);
}


/* Read one register */
static int ds3231_read_reg(struct i2c_client *client, u8 reg)
{
    return i2c_smbus_read_byte_data(client, reg);
}
static int ds3231_write_reg(struct i2c_client *client,
                            u8 reg, u8 val)
{
/* Write one byte to DS3231 register */
    u8 buf[2];
    int ret;

    buf[0] = reg;   // Register address
    buf[1] = val;   // Data to write

    /* Send over I2C */
    ret = i2c_master_send(client, buf, 2);

    if (ret <0) {
        dev_err(&client->dev,
                "DS3231 write failed (reg=0x%02x)\n",
                reg);
        return -EIO;
    }

    return 0;
}


/* Read full time */
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

    if (sec < 0 || min < 0 || hour < 0) {
        dev_err(&client->dev, "RTC Read Failed\n");
        return;
    }

    sec  = bcd_to_dec(sec);
    min  = bcd_to_dec(min);
    hour = bcd_to_dec(hour);
    day  = bcd_to_dec(day);
    date = bcd_to_dec(date);
    mon  = bcd_to_dec(mon);
    year = bcd_to_dec(year);

    dev_info(&client->dev,
        "Time: %02d:%02d:%02d  Date: %02d/%02d/%d  Day:%s\n",
        hour, min, sec, date, mon, year, days[day-1]);
}
static u8 dec_to_bcd(int val)
{
    return ((val / 10) << 4) | (val % 10);
}

static void ds3231_set_time(struct i2c_client *client,
                            int hour, int min, int sec,
                            int date, int mon, int year,int day)
{
    ds3231_write_reg(client, 0x00, dec_to_bcd(sec));   // Seconds
    ds3231_write_reg(client, 0x01, dec_to_bcd(min));   // Minutes
    ds3231_write_reg(client, 0x02, dec_to_bcd(hour));  // Hours
    ds3231_write_reg(client, 0x04, dec_to_bcd(date));  // Date
    ds3231_write_reg(client, 0x05, dec_to_bcd(mon));   // Month
    ds3231_write_reg(client, 0x06, dec_to_bcd(year));  // Year (0–99)
    ds3231_write_reg(client,0x03,dec_to_bcd(day));
    dev_info(&client->dev, "RTC time set\n");
}
/* Probe */
static int ds3231_probe(struct i2c_client *client)
{
    dev_info(&client->dev, "DS3231 RTC detected\n");
 ds3231_set_time(client,
                11,17, 0,   // HH:MM:SS
                16, 2, 26,2);  // DD/MM/YY

    /* Read time once */
    ds3231_read_time(client);

    return 0;
}


/* Remove */
static void ds3231_remove(struct i2c_client *client)
{
    dev_info(&client->dev, "DS3231 RTC removed\n");
}


/* Device Tree Match */
static const struct of_device_id ds3231_dt_ids[] = {
    { .compatible = "maxim,ds3231" },
    { }
};

MODULE_DEVICE_TABLE(of, ds3231_dt_ids);


/* I2C Driver */
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
MODULE_AUTHOR("techdhaba");
MODULE_DESCRIPTION("Simple DS3231 RTC I2C Driver");
