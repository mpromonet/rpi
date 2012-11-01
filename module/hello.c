/*  hello-3.c - Illustrating the __init, __initdata and __exit macros.
 *
 *  Copyright (C) 2001 by Peter Jay Salzman
 *
 *  08/02/2006 - Updated by Rodrigo Rubira Branco <rodrigo@kernelhacking.com>
 */

/* Kernel Programming */
#define MODULE
#define LINUX
#define __KERNEL__

#include <linux/module.h>      /* Needed by all modules */
#include <linux/kernel.h>      /* Needed for KERN_ALERT */
#include <linux/init.h>        /* Needed for the macros */

static int hello3_data __initdata = 3;


static int __init hello_3_init(void)
{
   printk(KERN_ALERT "Hello, world %d\n", hello3_data);
   return 0;
}


static void __exit hello_3_exit(void)
{
   printk(KERN_ALERT "Goodbye, world 3\n");
}


module_init(hello_3_init);
module_exit(hello_3_exit);

MODULE_LICENSE("GPL");

