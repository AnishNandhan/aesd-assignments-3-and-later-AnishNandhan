/*
 * aesdchar.h
 *
 *  Created on: Oct 23, 2019
 *      Author: Dan Walkes
 */

#ifndef AESD_CHAR_DRIVER_AESDCHAR_H_
#define AESD_CHAR_DRIVER_AESDCHAR_H_

#include <linux/mutex.h>
#include <linux/list.h>
#include "aesd-circular-buffer.h"

#define AESD_DEBUG 1  //Remove comment on this line to enable debug
#define MAX_COMMAND_SIZE 512

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

struct aesd_cmd_str {
     char *str;
     size_t size;
     struct list_head node;
};

struct aesd_dev
{

    /**
    * TODO: Add structure(s) and locks needed to complete assignment requirements
    */
    struct aesd_circular_buffer circular_buffer;
    size_t size;
    struct list_head cmds;
    size_t cur_cmd_size;
    struct mutex lock;
    struct cdev cdev;     /* Char device structure      */
};


#endif /* AESD_CHAR_DRIVER_AESDCHAR_H_ */
