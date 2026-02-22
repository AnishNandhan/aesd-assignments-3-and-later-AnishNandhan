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

MODULE_AUTHOR("Anish Nandhan");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");

    struct aesd_dev *dev;
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);

    filp->private_data = dev;

    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");

    return 0;
}

// static size_t aesd_free_entry(struct aesd_buffer_entry *entry) {
//     if (!entry) {
//         return 0;
//     }
//     size_t retval;

//     kfree(entry->buffptr);
//     retval = entry->size;
//     memset(entry, 0, sizeof(struct aesd_buffer_entry));

//     return retval;
// }

// static int aesd_pop(struct aesd_dev *dev, struct aesd_buffer_entry *entry) {
//     struct aesd_circular_buffer *c_buf = &dev->circular_buffer;
//     struct aesd_buffer_entry *out_entry = &c_buf->entry[c_buf->out_offs];
//     int removed_bytes = 0;
    
//     while (out_entry != entry) {
//         out_entry = aesd_circular_buffer_remove_entry(c_buf);
//         removed_bytes += aesd_free_entry(out_entry);
//     }
//     dev->size -= removed_bytes;
//     return removed_bytes;
// } 

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    struct aesd_dev *dev = filp->private_data;
    struct aesd_circular_buffer *c_buf = &dev->circular_buffer;
    struct aesd_buffer_entry *entry;
    size_t entry_offset_byte;
    int removed_count;
    int i;
    ssize_t retval = 0;
    
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    
    if (mutex_lock_interruptible(&dev->lock)) {
        return -ERESTARTSYS;
    }
    if (*f_pos > dev->size) {
        PDEBUG("f_pos > dev->size");
        goto out;
    }
    entry = aesd_circular_buffer_find_entry_offset_for_fpos(c_buf, *f_pos, &entry_offset_byte);

    if (entry == NULL || !entry->buffptr || entry->buffptr == 0) {
        PDEBUG("could not find valid entry for f_pos: %zu", *f_pos);
        goto out;
    }

    PDEBUG("found valid entry for f_pos: %zu", *f_pos);

    if (count > entry->size - entry_offset_byte) {
        count = entry->size - entry_offset_byte;
    }

    if (copy_to_user(buf, entry->buffptr, count)) {
        retval = -EFAULT;
        goto out;
    }

    PDEBUG("copied %zu bytes to user", count);

    *f_pos += count;
    retval = count;

  out:
    mutex_unlock(&dev->lock);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    struct aesd_dev *dev = filp->private_data;
    struct aesd_circular_buffer *c_buf = &dev->circular_buffer;
    struct aesd_buffer_entry add_entry, rm_entry;
    struct aesd_cmd_str *cmd, *e, *n;
    char *newline, *buffptr;
    size_t copied_size = 0;
    ssize_t retval = -ENOMEM;

    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);

    if (mutex_lock_interruptible(&dev->lock)) {
        return -ERESTARTSYS;
    }
    
    cmd = kzalloc(sizeof(struct aesd_cmd_str), GFP_KERNEL);
    if (!cmd) {
        goto out;
    }

    cmd->str = kzalloc(count, GFP_KERNEL);
    if (!cmd->str) {
        kfree(cmd);
        goto out;
    }

    if (copy_from_user(cmd->str, buf, count)) {
        kfree(cmd->str);
        kfree(cmd);
        retval = -EFAULT;
        goto out;
    }
    cmd->size = count;

    list_add_tail(&cmd->node, &dev->cmds);
    dev->cur_cmd_size += count;
    PDEBUG("current command size: %zu", dev->cur_cmd_size);

    newline = memchr(cmd->str, '\n', count);

    // Assumption: If '\n' is present there won't be any data following it
    if (newline) {
        PDEBUG("newline found in cmd");
        buffptr = kzalloc(dev->cur_cmd_size, GFP_KERNEL);
        if (!buffptr) {
            goto out;
        }
        list_for_each_entry_safe(e, n, &dev->cmds, node) {
            memcpy(buffptr + copied_size, e->str, e->size);
            copied_size += e->size;
            kfree(e->str);
            list_del(&e->node);
            kfree(e);
        }
        add_entry.buffptr = buffptr;
        add_entry.size = copied_size;
        rm_entry = aesd_circular_buffer_add_entry(c_buf, &add_entry);
        if (rm_entry.size) {
            dev->size -= rm_entry.size;
            kfree(rm_entry.buffptr);
        }
        dev->size += copied_size;
        dev->cur_cmd_size = 0;
        PDEBUG("written command with %zu bytes", copied_size);
    }

    *f_pos += count;
    retval = count;

  out:
    mutex_unlock(&dev->lock);
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
    INIT_LIST_HEAD(&aesd_device.cmds);
    
    mutex_init(&aesd_device.lock);

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }

    return result;

}

void aesd_cleanup_module(void)
{
    uint8_t index;
    struct aesd_buffer_entry *entry;
    struct aesd_cmd_str *e, *n;
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.circular_buffer, index) {
        kfree(entry->buffptr);
    }

    list_for_each_entry_safe(e, n, &aesd_device.cmds, node) {
        kfree(e->str);
        list_del(&e->node);
        kfree(e);
    }

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
