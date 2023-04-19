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
#include <linux/slab.h>
#include <linux/poll.h>


MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("XueYang, National Cheng Kung University, Taiwan");
MODULE_DESCRIPTION("kfifo char device driver");
MODULE_VERSION("0.1");

#define DEV_KFIFO_NAME "kdrv"
#define NO_OF_DEVICES 5
#define KFIFO_SIZE 100

/* Device private data structure */
struct kdev_private_data {
    char name[64];
    wait_queue_head_t read_queue;
    wait_queue_head_t write_queue;
    struct kfifo myfifo;
    struct cdev cdev;
};

/* Driver private data structure */
struct kdrv_driver_private_data {
    dev_t device_number;
    struct class *kclass;
    struct device *kdevice;
    struct kdev_private_data kdev_data[NO_OF_DEVICES];
};

static struct kdrv_driver_private_data kdrv_data;

static unsigned int kfifo_poll(struct file *file, poll_table *wait)
{
    int mask = 0;
    struct kdev_private_data *data = (struct kdev_private_data *) file->private_data;

    poll_wait(file, &data->read_queue, wait);
    poll_wait(file, &data->write_queue, wait);

    if (!kfifo_is_empty(&data->myfifo))
        mask |= POLLIN | POLLRDNORM;
    if (!kfifo_is_full(&data->myfifo))
        mask |= POLLOUT | POLLWRNORM;

    return mask;
}

static ssize_t kfifo_read(struct file *file,
                          char __user *buf,
                          size_t size,
                          loff_t *offset)
{
    int ret;
    int actual_readed;
    struct kdev_private_data *data = (struct kdev_private_data *)file->private_data;

    if (kfifo_is_empty(&data->myfifo)) {
        if (file->f_flags & O_NONBLOCK)
            return -EAGAIN;
        
        printk("%s: pid=%d, going to sleep\n", __func__, current->pid);
        ret = wait_event_interruptible(data->read_queue, !kfifo_is_empty(&data->myfifo));
        if (ret)
            return ret;
    }

    ret = kfifo_to_user(&data->myfifo, buf, size, &actual_readed);
    if (ret)
        return -EIO;

    /* There are spaces let user to write data to kfifo. */
    if (!kfifo_is_full(&data->myfifo))
        wake_up_interruptible(&data->write_queue);

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
    struct kdev_private_data *data = (struct kdev_private_data *)file->private_data;

    if (kfifo_is_full(&data->myfifo)) {
        if (file->f_flags & O_NONBLOCK)
            return -EAGAIN;
        printk("%s: pid=%d, going to sleep\n", __func__, current->pid);
        ret = wait_event_interruptible(data->write_queue, !kfifo_is_full(&data->myfifo));
        if (ret)
            return ret;
    }

    ret = kfifo_from_user(&data->myfifo, buf, size, &actual_write);
    if (ret)
        return -EIO;

    /* There is data that user can read from kfifo. */
    if (!kfifo_is_empty(&data->myfifo))
        wake_up_interruptible(&data->read_queue);

    
    printk("%s, pid=%d, actual_write=%d, pos=%lld\n", __func__, current->pid, actual_write, *offset);
    return actual_write;
}

static int kfifo_open(struct inode *inode, struct file *file)
{
    unsigned int minor_n;
    minor_n = MINOR(inode->i_rdev);
    pr_info("minor access = %d\n",minor_n);
    
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
    .poll = kfifo_poll,
};

static int __init init_kfifo_dev(void)
{
    int ret = 0;
    int i;

    //Dynamically allocate a device major number
    ret = alloc_chrdev_region(&kdrv_data.device_number, 0, NO_OF_DEVICES, DEV_KFIFO_NAME);
    if (ret < 0) {
        printk(KERN_ALERT
               "Failed to register the KFIFO char device. rc = %i",
               ret);
        return ret;
    }

    /* Create device class under /sys/class */
    kdrv_data.kclass = class_create(THIS_MODULE, DEV_KFIFO_NAME);
    if (IS_ERR(kdrv_data.kclass)) {
        printk(KERN_ALERT "Failed to create cleass");
        goto failed_cdev;
    }

    for (i = 0; i < NO_OF_DEVICES; i++) {
        pr_info("Device number <major>:<minor>= %d:%d\n", MAJOR(kdrv_data.device_number + i), MINOR(kdrv_data.device_number + i));

        /* Initialize the cdev structure with fops */
        cdev_init(&kdrv_data.kdev_data[i].cdev, &kfifo_fops);
        kdrv_data.kdev_data[i].cdev.owner = THIS_MODULE;
        
        /* Register a device (cdev structure) with VFS */
        ret = cdev_add(&kdrv_data.kdev_data[i].cdev, kdrv_data.device_number + i, 1);
        if (ret < 0) {
            pr_err("cdev add failed\n");
            goto failed_cdev;
        }

        /* Populate the sysfs with device information */
        sprintf(kdrv_data.kdev_data[i].name, "%s%d", DEV_KFIFO_NAME, i);
        kdrv_data.kdevice = device_create(kdrv_data.kclass, NULL, kdrv_data.device_number + i, NULL, "kdrv-%d", i + 1);
        if (IS_ERR(kdrv_data.kdevice)) {
            pr_err("Device create failed\n");
            goto failed_class_create;
        }

        init_waitqueue_head(&kdrv_data.kdev_data[i].read_queue);
        init_waitqueue_head(&kdrv_data.kdev_data[i].write_queue);

        ret = kfifo_alloc(&kdrv_data.kdev_data[i].myfifo, KFIFO_SIZE, GFP_KERNEL);
        if (ret) {
            ret = -ENOMEM;
            goto failed_create_kfifo;
        }
    }

    printk("succeeded register char device : %s\n", DEV_KFIFO_NAME);
    return ret;


failed_create_kfifo:
    for(int i = 0; i < NO_OF_DEVICES; i++) {
        if (&kdrv_data.kdev_data[i].myfifo) 
            kfifo_free(&kdrv_data.kdev_data[i].myfifo);
    }
failed_class_create:
    for (; i >= 0; i--) {
        device_destroy(kdrv_data.kclass, kdrv_data.device_number + i);
        cdev_del(&kdrv_data.kdev_data[i].cdev);
    }
    class_destroy(kdrv_data.kclass);
failed_cdev:
    unregister_chrdev_region(kdrv_data.device_number, NO_OF_DEVICES);
    return ret;
}

static void __exit exit_kfifo_dev(void)
{
    for (int i = 0; i < NO_OF_DEVICES; i++) {
        device_destroy(kdrv_data.kclass, kdrv_data.device_number + i);
        cdev_del(&kdrv_data.kdev_data[i].cdev);
    }
    class_destroy(kdrv_data.kclass);
    unregister_chrdev_region(kdrv_data.device_number, NO_OF_DEVICES);
    pr_info("kdrv module unloaded\n");
}

module_init(init_kfifo_dev);
module_exit(exit_kfifo_dev);