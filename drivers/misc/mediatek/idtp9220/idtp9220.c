/*
** =============================================================================
** Copyright (c) 2012  Immersion Corporation.
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public License
** as published by the Free Software Foundation; either version 2
** of the License, or (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**
** File:
**     idtp9220.c
**
** Description:
**     IDTP9220 chip driver
**
** =============================================================================
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include <linux/slab.h>
#include <linux/types.h>

#include <linux/fs.h>
#include <linux/cdev.h>

#include <linux/i2c.h>
#include <linux/semaphore.h>
#include <linux/device.h>

#include <linux/syscalls.h>
#include <asm/uaccess.h>

#include <linux/gpio.h>

#include <linux/sched.h>


#include <linux/spinlock_types.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/timer.h>

#include <linux/workqueue.h>
#include <../../../drivers/staging/android/timed_output.h>
#include <linux/hrtimer.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/clk.h>
#include <linux/wakelock.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <mach/mt_gpio.h>
#include <mach/mt_reg_base.h>
#include <mach/mt_typedefs.h>
#include "cust_gpio_usage.h"


// vibrator .h startup
#include <mach/upmu_common_sw.h>
#include <mach/upmu_hw.h>

#define DEVICE_NAME "idtp9220"
#define DRIVER_VERSION "99"

/* Address of our device */
#define DEVICE_ADDR 0x61

/* i2c bus that it sits on */
#define DEVICE_BUS  1

static struct idtp9220 {
	struct class *class;
	struct device *device;
	dev_t version;
	struct i2c_client *client;
	struct semaphore sem;
	struct cdev cdev;
	int en_gpio;
	int external_trigger;
	int trigger_gpio;
} *idtp9220;

static char read_val;

static ssize_t idtp9220_read(struct file *filp, char *buff, size_t length,
			    loff_t * offset)
{
	buff[0] = read_val;
	return 1;
}

static struct file_operations fops = {
	.read = idtp9220_read
};




static int idtp9220_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{


	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		printk(KERN_ALERT "idtp9220 probe failed");
		return -ENODEV;
	}

	udelay(850);
	/* Read status */

	mt_set_gpio_mode(GPIO138, GPIO_MODE_00);
	mt_set_gpio_dir(GPIO138, 1);

	//Luxios 0 = enable ic, 1 = disable ic
	mt_set_gpio_out(GPIO138, 0);

	mt_set_gpio_mode(GPIO63, 3);//GPIO_MODE_03??
        mt_set_gpio_dir(GPIO63, 0);
        mt_set_gpio_pull_select(GPIO63, 1);
        mt_set_gpio_pull_enable(GPIO63, 0);

	printk(KERN_ALERT "idtp9220: probe test\n");
	return 0;
}

static int idtp9220_remove(struct i2c_client *client)
{
	printk(KERN_ALERT "idtp9220 remove");
	return 0;
}


static struct i2c_device_id idtp9220_id_table[] = {
	{DEVICE_NAME, 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, idtp9220_id_table);

static struct i2c_driver idtp9220_driver = {
	.driver = {
		  .name = DEVICE_NAME,
		  },
	.id_table = idtp9220_id_table,
	.probe = idtp9220_probe,
	.remove = idtp9220_remove
};

static struct i2c_board_info __initdata i2c_idtp9220_boardinfo =
{
	I2C_BOARD_INFO("idtp9220", (DEVICE_ADDR))
};

static int idtp9220_init(void)
{
	int reval = -ENOMEM;

	idtp9220 = kzalloc(sizeof(struct idtp9220), GFP_KERNEL);
	if (!idtp9220) {
		printk(KERN_ALERT
		       "idtp9220: cannot allocate memory for idtp9220 driver\n");
		goto fail0;
	}

	i2c_register_board_info(DEVICE_BUS, &i2c_idtp9220_boardinfo, 1);

	reval = i2c_add_driver(&idtp9220_driver);
	if (reval) {
		printk(KERN_ALERT "idtp9220 driver initialization error \n");
		goto fail2;
	}

	idtp9220->version = MKDEV(0, 0);
	reval = alloc_chrdev_region(&idtp9220->version, 0, 1, DEVICE_NAME);
	if (reval < 0) {
		printk(KERN_ALERT "idtp9220: error getting major number %d\n",
		       reval);
		goto fail3;
	}

	idtp9220->class = class_create(THIS_MODULE, DEVICE_NAME);
	if (!idtp9220->class) {
		printk(KERN_ALERT "idtp9220: error creating class\n");
		goto fail4;
	}

	idtp9220->device =
	    device_create(idtp9220->class, NULL, idtp9220->version, NULL,
			  DEVICE_NAME);
	if (!idtp9220->device) {
		printk(KERN_ALERT "idtp9220: error creating device idtp9220\n");
		goto fail5;
	}

	cdev_init(&idtp9220->cdev, &fops);
	idtp9220->cdev.owner = THIS_MODULE;
	idtp9220->cdev.ops = &fops;
	reval = cdev_add(&idtp9220->cdev, idtp9220->version, 1);

	if (reval) {
		printk(KERN_ALERT "idtp9220: fail to add cdev\n");
		goto fail6;
	}

	mt_set_gpio_mode(GPIO138, GPIO_MODE_00);
	mt_set_gpio_dir(GPIO138, 1);
	mt_set_gpio_out(GPIO138, 0);

	mt_set_gpio_mode(GPIO63, 3);//GPIO_MODE_03??
        mt_set_gpio_dir(GPIO63, 0);
        mt_set_gpio_pull_select(GPIO63, 1);
        mt_set_gpio_pull_enable(GPIO63, 0);
        //mt_eint_registration(3, 2, 0xC0576B38, 1);
        //mt_eint_mask(3);

	

	printk(KERN_ALERT "idtp9220: initialized init and gpio\n");
	return 0;

 fail6:
	device_destroy(idtp9220->class, idtp9220->version);
 fail5:
	class_destroy(idtp9220->class);
 fail4:
 fail3:
	i2c_del_driver(&idtp9220_driver);
 fail2:
	kfree(idtp9220);
 fail0:
	return reval;
}

static void idtp9220_exit(void)
{

	device_destroy(idtp9220->class, idtp9220->version);
	class_destroy(idtp9220->class);
	unregister_chrdev_region(idtp9220->version, 1);
	i2c_unregister_device(idtp9220->client);
	kfree(idtp9220);
	i2c_del_driver(&idtp9220_driver);

	printk(KERN_ALERT "idtp9220: exit\n");
}

module_init(idtp9220_init);
module_exit(idtp9220_exit);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Luxios porting wireless charger");
MODULE_DESCRIPTION("Driver for " DEVICE_NAME);
