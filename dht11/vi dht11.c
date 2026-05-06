#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Embedded Developer");
MODULE_DESCRIPTION("Improved DHT11 character driver");
MODULE_VERSION("2.0");

#define DEVICE_NAME "dht11"
#define MAJOR_NUM 240
#define GPIO_DATA_PIN 4   // ✅ FIXED

/* Wait while pin is at expected level, return duration */
static int read_pulse(int expected_level)
{
    int count = 0;

    while (gpio_get_value(GPIO_DATA_PIN) == expected_level) {
        udelay(1);
        if (++count > 100)   // ~100us timeout
            return -1;
    }
    return count;
}

static int read_dht11_data(int *temp, int *hum)
{
    uint8_t data[5] = {0};
    int i;
    unsigned long flags;

    /* Start signal */
    gpio_direction_output(GPIO_DATA_PIN, 0);
    msleep(20);                    // >18ms
    gpio_set_value(GPIO_DATA_PIN, 1);
    udelay(40);

    gpio_direction_input(GPIO_DATA_PIN);

    local_irq_save(flags);

    /* Sensor response */
    if (read_pulse(0) < 0 || read_pulse(1) < 0) {
        local_irq_restore(flags);
        return -EIO;
    }

    /* Read 40 bits */
    for (i = 0; i < 40; i++) {

        if (read_pulse(0) < 0) {
            local_irq_restore(flags);
            return -EIO;
        }

        int high_time = read_pulse(1);
        if (high_time < 0) {
            local_irq_restore(flags);
            return -EIO;
        }

        data[i / 8] <<= 1;

        /* ✅ Better threshold (~50us) */
        if (high_time > 50)
            data[i / 8] |= 1;
    }

    local_irq_restore(flags);

    /* Debug (optional) */
    pr_debug("RAW: %d %d %d %d %d\n",
             data[0], data[1], data[2], data[3], data[4]);

    /* Checksum */
    if (data[4] != ((data[0] + data[1] + data[2] + data[3]) & 0xFF))
        return -EBADMSG;

    *hum = data[0];
    *temp = data[2];

    return 0;
}

static ssize_t device_read(struct file *filp, char __user *buffer,
                           size_t length, loff_t *offset)
{
    int temp = 0, hum = 0, ret;
    char result[64];
    int len;

    if (*offset > 0)
        return 0;

    ret = read_dht11_data(&temp, &hum);

    if (ret == -EIO)
        len = scnprintf(result, sizeof(result),
                        "Error: Sensor timeout\n");
    else if (ret == -EBADMSG)
        len = scnprintf(result, sizeof(result),
                        "Error: Checksum mismatch\n");
    else
        len = scnprintf(result, sizeof(result),
                        "Humidity: %d%% | Temperature: %d°C\n",
                        hum, temp);

    if (copy_to_user(buffer, result, len))
        return -EFAULT;

    *offset += len;
    return len;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .read = device_read,
};

static int __init dht11_init(void)
{
    int ret;

    pr_info("dht11: init\n");

    ret = register_chrdev(MAJOR_NUM, DEVICE_NAME, &fops);
    if (ret < 0) {
        pr_err("dht11: chrdev failed\n");
        return ret;
    }

    if (!gpio_is_valid(GPIO_DATA_PIN)) {
        pr_err("Invalid GPIO\n");
        unregister_chrdev(MAJOR_NUM, DEVICE_NAME);
        return -EINVAL;
    }

    ret = gpio_request(GPIO_DATA_PIN, "dht11");
    if (ret) {
        unregister_chrdev(MAJOR_NUM, DEVICE_NAME);
        return ret;
    }

    gpio_direction_output(GPIO_DATA_PIN, 1);

    return 0;
}

static void __exit dht11_exit(void)
{
    gpio_free(GPIO_DATA_PIN);
    unregister_chrdev(MAJOR_NUM, DEVICE_NAME);
    pr_info("dht11: exit\n");
}

module_init(dht11_init);
module_exit(dht11_exit);
