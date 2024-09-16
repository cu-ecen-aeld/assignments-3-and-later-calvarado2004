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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include "aesdchar.h"
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
    struct aesd_dev *dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev; // Associate file with device structure
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    // Free any partially accumulated write entry
    if (dev->write_entry.buffptr != NULL) {
        kfree(dev->write_entry.buffptr);
        dev->write_entry.buffptr = NULL;
        dev->write_entry.size = 0;
    }

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
    struct aesd_buffer_entry *entry;
    size_t entry_offset = 0, bytes_to_copy = 0;

    if (mutex_lock_interruptible(&dev->mutex)) {
        return -ERESTARTSYS;
    }

    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->circular_buffer, *f_pos, &entry_offset);
    if (entry == NULL) {
        mutex_unlock(&dev->mutex);
        return 0; // EOF
    }

    bytes_to_copy = min(count, entry->size - entry_offset);
    if (copy_to_user(buf, entry->buffptr + entry_offset, bytes_to_copy)) {
        retval = -EFAULT;
    } else {
        *f_pos += bytes_to_copy;
        retval = bytes_to_copy;
    }

    mutex_unlock(&dev->mutex);
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
    size_t i, newline_pos = 0;
    bool found_newline = false;

    kbuf = kmalloc(count, GFP_KERNEL);
    if (!kbuf) return -ENOMEM;

    if (copy_from_user(kbuf, buf, count)) {
        kfree(kbuf);
        return -EFAULT;
    }

    // Search for newline in the buffer
    for (i = 0; i < count; i++) {
        if (kbuf[i] == '\n') {
            newline_pos = i + 1;  // Include the newline character
            found_newline = true;
            break;
        }
    }

    if (mutex_lock_interruptible(&dev->mutex)) {
        kfree(kbuf);
        return -ERESTARTSYS;
    }

    // If found newline, write up to the newline to the buffer
    if (found_newline) {
        size_t new_size = dev->write_entry.size + newline_pos;
        dev->write_entry.buffptr = krealloc(dev->write_entry.buffptr, new_size, GFP_KERNEL);
        if (!dev->write_entry.buffptr) {
            kfree(kbuf);
            retval = -ENOMEM;
            goto out;
        }

        memcpy(dev->write_entry.buffptr + dev->write_entry.size, kbuf, newline_pos);
        dev->write_entry.size = new_size;

        // Add the entry to the circular buffer
        aesd_circular_buffer_add_entry(&dev->circular_buffer, &dev->write_entry);

        // Reset the write_entry for next write
        dev->write_entry.buffptr = NULL;
        dev->write_entry.size = 0;

        retval = newline_pos;
    } else {
        // No newline found, accumulate data
        size_t new_size = dev->write_entry.size + count;
        dev->write_entry.buffptr = krealloc(dev->write_entry.buffptr, new_size, GFP_KERNEL);
        if (!dev->write_entry.buffptr) {
            kfree(kbuf);
            retval = -ENOMEM;
            goto out;
        }

        memcpy(dev->write_entry.buffptr + dev->write_entry.size, kbuf, count);
        dev->write_entry.size += count;

        retval = count;
    }

out:
    mutex_unlock(&dev->mutex);
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

    // Initialize the AESD-specific portion of the device
    aesd_circular_buffer_init(&aesd_device.circular_buffer);
    mutex_init(&aesd_device.mutex);

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
    // Free the circular buffer entries
    for (i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++) {
        kfree(aesd_device.circular_buffer.entry[i].buffptr);
    }

    mutex_destroy(&aesd_device.mutex);

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);