#include <linux/module.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>

#define DEV_NAME "mydevice"

static dev_t dev_num;
static struct class *my_class;
static struct cdev my_cdev;

static int my_open(struct inode *inode, struct file *file)
{
    pr_info("device opened\n");
    return 0;
}

static int my_release(struct inode *inode, struct file *file)
{
    pr_info("device closed\n");
    return 0;
}

static ssize_t my_read(struct file *f,
                       char __user *buf,
                       size_t len,
                       loff_t *off)
{
    return 0;
}

static struct file_operations fops = {
    .owner   = THIS_MODULE,
    .open    = my_open,
    .release = my_release,
    .read    = my_read,
};

static int __init my_init(void)
{
    alloc_chrdev_region(&dev_num, 0, 1, DEV_NAME);

    cdev_init(&my_cdev, &fops);
    cdev_add(&my_cdev, dev_num, 1);

    my_class = class_create("myclass");

    device_create(my_class,
                  NULL,
                  dev_num,
                  NULL,
                  DEV_NAME);

    pr_info("Driver loaded\n");

    return 0;
}

static void __exit my_exit(void)
{
    device_destroy(my_class, dev_num);
    class_destroy(my_class);

    cdev_del(&my_cdev);

    unregister_chrdev_region(dev_num, 1);

    pr_info("Driver unloaded\n");
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
