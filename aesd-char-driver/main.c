/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes, modified by MacPiper for Assignment 8
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
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "aesdchar.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Your Name Here"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev *dev;

    PDEBUG("open");
    /**
     * TODO: handle open
    */
    /** Set filp->private_data with our aesd_dev device struct **/
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;


    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
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
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    /* open() stores the device pointer here so every file operation can find it. */
    struct aesd_dev *dev = filp->private_data;
    /* working is the not-yet-completed line being assembled across write calls. */
    struct aesd_buffer_entry *working;
    /* The completed entry passed to the circular buffer; it takes ownership
     * of new_buffer when added.
     */
    struct aesd_buffer_entry new_entry;
    /* A fresh accumulator is prepared before committing a completed entry. */
    struct aesd_buffer_entry *new_working = NULL;
    /* input is a kernel-space copy of the user's data. */
    char *input = NULL;
    /* Holds the old partial line plus this write, before it is committed. */
    char *new_buffer = NULL;
    size_t new_size;
    /* Allocation failures are reported unless a later operation succeeds. */
    ssize_t retval = -ENOMEM;

    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    /* There is nothing to append, and no state needs to be changed. */
    if (count == 0)
        return 0;

    /* User memory cannot safely be read directly or while holding the mutex.
     * Copy it first, then operate only on kernel memory under the lock.
     */
    input = kmalloc(count, GFP_KERNEL);
    if (!input)
        return -ENOMEM;
    if (copy_from_user(input, buf, count)) {
        /* copy_from_user returns the number of bytes it could not copy. */
        retval = -EFAULT;
        goto out;
    }

    /* The working line and circular buffer are shared by all opens of this
     * device, so serialize changes to them. Allow a pending signal to interrupt
     * the wait instead of leaving this write blocked indefinitely.
     */
    if (mutex_lock_interruptible(&dev->lock)) {
        retval = -ERESTARTSYS;
        goto out;
    }
    working = dev->working_buffer_entry;
    if (!working) {
        /* No partial line exists yet; allocate an empty entry to accumulate
         * data until a write ends in a newline.
         */
        working = kmalloc(sizeof(*working), GFP_KERNEL);
        if (!working)
            goto unlock;
        working->buffptr = NULL;
        working->size = 0;
        dev->working_buffer_entry = working;
    }

    /* Guard the size addition against wrapping, which could otherwise result
     * in allocating too little memory for the combined data.
     */
    if (count > (size_t)-1 - working->size) {
        retval = -EOVERFLOW;
        goto unlock;
    }
    new_size = working->size + count;
    /* Allocate a replacement rather than growing in place. This keeps the
     * existing partial line intact if allocation fails.
     */
    new_buffer = kmalloc(new_size, GFP_KERNEL);
    if (!new_buffer)
        goto unlock;

    /* Build the next line from the bytes accumulated by earlier writes and
     * the bytes supplied by this call.
     */
    if (working->size)
        memcpy(new_buffer, working->buffptr, working->size);
    memcpy(new_buffer + working->size, input, count);

    /* A newline at the end of this write marks the assembled data as one
     * complete circular-buffer entry.
     */
    if (input[count - 1] == '\n') {
        /* Allocate the next accumulator before modifying the circular buffer.
         * If this allocation fails, the current partial line remains intact.
         */
        PDEBUG("write: complete line, adding to circular buffer");
        new_working = kmalloc(sizeof(*new_working), GFP_KERNEL);
        if (!new_working)
            goto unlock;
        new_working->buffptr = NULL;
        new_working->size = 0;

        new_entry.buffptr = new_buffer;
        new_entry.size = new_size;
        /* When full, in_offs identifies the slot that add_entry will replace.
         * Free that slot's owned data before overwriting its pointer.
         */
        if (dev->circular_buffer.full)
            kfree((void *)dev->circular_buffer
                      .entry[dev->circular_buffer.in_offs].buffptr);
        /* The circular buffer copies the entry descriptor. The allocated
         * contents remain owned by that slot until it is evicted or cleaned up.
         */
        /* TODO: Implement freeing an abandoned entry, this will be passed by the function as return value,
        otherwise it will return NULL. This is not implemented in the function itself */
        aesd_circular_buffer_add_entry(&dev->circular_buffer, &new_entry);
        /* The old accumulator is no longer needed: its contents are now owned
         * by the circular buffer through new_entry.
         */
        kfree((void *)working->buffptr);
        kfree(working);
        dev->working_buffer_entry = new_working;
        /* These pointers now belong to the device state, so prevent the
         * common cleanup below from freeing them.
         */
        new_working = NULL;
        new_buffer = NULL;
    } else {
        /* The line is still incomplete. Replace the old accumulator contents
         * with the longer version and keep it for the next write call.
         */
        kfree((void *)working->buffptr);
        working->buffptr = new_buffer;
        working->size = new_size;
        /* The working entry now owns this buffer. */
        new_buffer = NULL;
    }
    retval = count;

unlock:
    /* All exits after acquiring the mutex pass here, including allocation or
     * size-overflow failures.
     */
    mutex_unlock(&dev->lock);
    /* Free only allocations not transferred into the device's persistent
     * working entry or circular buffer.
     */
    kfree(new_working);
    kfree(new_buffer);
out:
    /* input is temporary for every path after it was allocated. */
    kfree(input);
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
    /* Initialize the lock before the device becomes available to callers. */
    mutex_init(&aesd_device.lock);
    /* Start with no completed write entries in the circular buffer. */
    aesd_circular_buffer_init(&aesd_device.circular_buffer);

    /**
     * TODO: initialize the AESD specific portion of the device
     */

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);
    uint8_t index;

    cdev_del(&aesd_device.cdev);

    /* Release the unfinished line, if any, and its separately allocated
     * descriptor.
     */
    if (aesd_device.working_buffer_entry) {
        kfree((void *)aesd_device.working_buffer_entry->buffptr);
        kfree(aesd_device.working_buffer_entry);
    }
    /* Entries own their data buffers. Empty slots contain NULL pointers, for
     * which kfree is safe, so this also handles a partially filled buffer.
     */
    for (index = 0; index < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; index++)
        kfree((void *)aesd_device.circular_buffer.entry[index].buffptr);

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
