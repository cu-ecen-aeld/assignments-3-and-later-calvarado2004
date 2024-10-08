/*
 * aesdchar.h
 *
 *  Created on: Oct 23, 2019
 *      Author: Dan Walkes
 */

#include <linux/cdev.h>    // For struct cdev
#include <linux/mutex.h>   // For struct mutex
#include "aesd-circular-buffer.h"  // Include the circular buffer header

#ifndef AESD_CHAR_DRIVER_AESDCHAR_H_
#define AESD_CHAR_DRIVER_AESDCHAR_H_
#define AESDCHAR_MAX_WRITE_SIZE 1024

#define AESD_DEBUG 1  //Remove comment on this line to enable debug

#undef PDEBUG             /* undef it, just in case */
#ifdef AESD_DEBUG
#  ifdef __KERNEL__
     /* This one if debugging is on, and kernel space */
#    define PDEBUG(fmt, args...) printk( KERN_DEBUG "aesdchar: " fmt, ## args)
#  else
     /* This one for user space */
#    define PDEBUG(fmt, args...) fprintf(stderr, fmt, ## args)
#  endif
#else
#  define PDEBUG(fmt, args...) /* not debugging: nothing */
#endif

struct aesd_dev
{
    /**
     * TODO: Add structure(s) and locks needed to complete assignment requirements
     */

    struct cdev cdev;     /* Char device structure      */
    struct aesd_circular_buffer buffer; /* Circular buffer for write operations */
    struct mutex lock;   /* Mutex to synchronize access */
    char partial_write_buffer[AESDCHAR_MAX_WRITE_SIZE]; // Buffer for partial writes
    size_t partial_write_size;  // Current size of the partial write buffer

};

/* Function prototypes for file operations */
int aesd_open(struct inode *inode, struct file *filp);
int aesd_release(struct inode *inode, struct file *filp);
ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos);
ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos);
loff_t aesd_llseek(struct file *filp, loff_t offset, int whence);
long aesd_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

/* Function prototypes for module initialization and cleanup */
int aesd_init_module(void);
void aesd_cleanup_module(void);

#endif /* AESD_CHAR_DRIVER_AESDCHAR_H_ */