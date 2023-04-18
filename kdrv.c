#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/cdev.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/kfifo.h>
#include <linux/mutex.h>
#include <linux/wait.h>


MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("XueYang, National Cheng Kung University, Taiwan");
MODULE_DESCRIPTION("kfifo char device driver");
MODULE_VERSION("0.1");

#define DEV_KFIFO_NAME "kdrv"

static dev_t kfifo_dev = 0;
static struct cdev *kfifo_cdev;
static struct class *kfifo_class;


DEFINE_KFIFO(myfifo, char, 64);
DECLARE_WAIT_QUEUE_HEAD(read_queue);
DECLARE_WAIT_QUEUE_HEAD(write_queue);

/**
 * ! Modify the char device driver's read and write operations to be blocking mode.
 * * How to test ?
 * * cat /dev/kdrv &
 * * echo "Testing linux kdrv" > /dev/kdrv
 * * dmesg
*/

static ssize_t kfifo_read(struct file *file,
                          char __user *buf,
                          size_t size,
                          loff_t *offset)
{
    int ret;
    int actual_readed;

    if (kfifo_is_empty(&myfifo)) {
        if (file->f_flags & O_NONBLOCK)
            return -EAGAIN;
        
        printk("%s: pid=%d, going to sleep\n", __func__, current->pid);
        ret = wait_event_interruptible(read_queue, !kfifo_is_empty(&myfifo));
        if (ret)
            return ret;
    }

    ret = kfifo_to_user(&myfifo, buf, size, &actual_readed);
    if (ret)
        return -EIO;

    /* There are spaces let user to write data to kfifo. */
    if (!kfifo_is_full(&myfifo))
        wake_up_interruptible(&write_queue);

    printk("%s, pid=%d, actual_readed=%d, pos=%lld\n", __func__, current->pid, actual_readed, *offset);
    return actual_readed;
}

static ssize_t kfifo_write(struct file *file,
                            const char __user *buf,
                            size_t size,
                            loff_t *offset)
{
    unsigned int actual_write;
    int ret;

    if (kfifo_is_full(&myfifo)) {
        if (file->f_flags & O_NONBLOCK)
            return -EAGAIN;
        printk("%s: pid=%d, going to sleep\n", __func__, current->pid);
        ret = wait_event_interruptible(write_queue, !kfifo_is_full(&myfifo));
        if (ret)
            return ret;
    }

    ret = kfifo_from_user(&myfifo, buf, size, &actual_write);
    if (ret)
        return -EIO;

    /* There is data that user can read from kfifo. */
    if (!kfifo_is_empty(&myfifo))
        wake_up_interruptible(&read_queue);

    
    printk("%s, pid=%d, actual_write=%d, pos=%lld\n", __func__, current->pid, actual_write, *offset);
    return actual_write;
}

static int kfifo_open(struct inode *inode, struct file *file)
{
    printk("Major number = %d, minor number = %d\n", MAJOR(inode->i_rdev), MINOR(inode->i_rdev));
    return 0;
}

static int kfifo_release(struct inode *inode, struct file *file)
{
    return 0;
}

const struct file_operations kfifo_fops = {
    .owner = THIS_MODULE,
    .read = kfifo_read,
    .write = kfifo_write,
    .open = kfifo_open,
    .release = kfifo_release,
};

static int __init init_kfifo_dev(void)
{
    int ret = 0;

    ret = alloc_chrdev_region(&kfifo_dev, 0, 1, DEV_KFIFO_NAME);
    if (ret < 0) {
        printk(KERN_ALERT
               "Failed to register the KFIFO char device. rc = %i",
               ret);
        return ret;
    }

    kfifo_cdev = cdev_alloc();
    if (!kfifo_cdev) {
        printk(KERN_ALERT "Failed to alloc cdev");
        ret = -1;
        goto failed_cdev;
    }

    /* link file operations */
    kfifo_cdev->ops = &kfifo_fops;
    ret = cdev_add(kfifo_cdev, kfifo_dev, 1);

    if (ret < 0) {
        printk(KERN_ALERT "Failed to add cdev");
        ret = -2;
        goto failed_cdev;
    }

    kfifo_class = class_create(THIS_MODULE, DEV_KFIFO_NAME);
    if (!kfifo_class) {
        printk(KERN_ALERT "Failed to create device class");
        ret = -3;
        goto failed_class_create;
    }

    if (!device_create(kfifo_class, NULL, kfifo_dev, NULL, DEV_KFIFO_NAME)) {
        printk(KERN_ALERT, "Failed to create device");
        ret = -4;
        goto failed_create_device;
    }

    printk("succeeded register char device : %s\n", DEV_KFIFO_NAME);
    return ret;

failed_create_device:
    class_destroy(kfifo_class);
failed_class_create:
    cdev_del(kfifo_cdev);
failed_cdev:
    unregister_chrdev_region(kfifo_dev, 1);
    return ret;
}

static void __exit exit_kfifo_dev(void)
{
    device_destroy(kfifo_class, kfifo_dev);
    class_destroy(kfifo_class);
    cdev_del(kfifo_cdev);
    unregister_chrdev_region(kfifo_dev, 1);
}

module_init(init_kfifo_dev);
module_exit(exit_kfifo_dev);