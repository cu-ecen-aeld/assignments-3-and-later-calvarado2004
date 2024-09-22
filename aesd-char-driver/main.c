/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */
#include <linux/slab.h>  // For kmalloc and kfree
#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include "aesdchar.h"
#include "aesd-circular-buffer.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Carlos Alvarado"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    /**
     * TODO: handle open
     */

    struct aesd_dev *dev;

    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;

    if (!dev) {
        return -EFAULT;
    }

    return 0;

}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */

    struct aesd_dev *dev;

    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;

    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle read
     */

    struct aesd_dev *dev = filp->private_data;
    size_t entry_offset = 0;
    size_t bytes_read = 0;
    struct aesd_buffer_entry *entry;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->buffer, *f_pos, &entry_offset);
    if (entry) {
        bytes_read = min(count, entry->size - entry_offset);
        if (copy_to_user(buf, entry->buffptr + entry_offset, bytes_read)) {
            retval = -EFAULT;
            goto out;
        } else {
            *f_pos += bytes_read;
            retval = bytes_read;
        }
    } else {
        retval = 0;  // Return 0 to indicate EOF
    }

out:
    mutex_unlock(&dev->lock);
    return retval;

}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle write
     */

    struct aesd_dev *dev = filp->private_data;
    char *kbuf;
    struct aesd_buffer_entry entry;
    int newline_found = 0;
    size_t i;

    if (!dev) {
        return -EFAULT;
    }

    kbuf = kmalloc(count, GFP_KERNEL);
    if (!kbuf) {
        return retval;
    }

    if (copy_from_user(kbuf, buf, count)) {
        retval = -EFAULT;
        goto out;
    }

    if (mutex_lock_interruptible(&dev->lock)) {
        retval = -ERESTARTSYS;
        goto out;
    }

    for (i = 0; i < count; i++) {
        dev->partial_write_buffer[dev->partial_write_size++] = kbuf[i];

        if (kbuf[i] == '\n') {
            newline_found = 1;

            // Allocate buffer for the entry
            entry.buffptr = kmalloc(dev->partial_write_size, GFP_KERNEL);
            if (!entry.buffptr) {
                retval = -ENOMEM;
                goto unlock_out;
            }

            // Copy the contents of partial write buffer to the circular buffer entry
            memcpy((void *)entry.buffptr, dev->partial_write_buffer, dev->partial_write_size);
            entry.size = dev->partial_write_size;

            // If circular buffer is full, free the old buffer
            if (dev->buffer.full) {
                kfree(dev->buffer.entry[dev->buffer.out_offs].buffptr); // Free old entry if buffer is full
            }

            aesd_circular_buffer_add_entry(&dev->buffer, &entry);

            dev->partial_write_size = 0; // Reset partial write size after adding to buffer
        }

        // Check for write size overflow
        if (dev->partial_write_size >= AESDCHAR_MAX_WRITE_SIZE) {
            retval = -ENOMEM;
            goto unlock_out;
        }
    }

    retval = count;

    unlock_out:
        mutex_unlock(&dev->lock);

    out:
        kfree(kbuf);
    return retval;

}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}



int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    /**
     * TODO: initialize the AESD specific portion of the device
     */

    mutex_init(&aesd_device.lock);  /* Initialize the mutex */
    aesd_circular_buffer_init(&aesd_device.buffer); /* Initialize the circular buffer */

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */

    struct aesd_buffer_entry *entry;
    uint8_t index;

    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.buffer, index) {
        if (entry->buffptr) {
            kfree(entry->buffptr);
        }
    }

    unregister_chrdev_region(devno, 1);
}


module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
