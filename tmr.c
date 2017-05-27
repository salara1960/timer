//****************** Linux kernel driver for timer **************************************
//
// Timer driver tmr_mod.ko, with an interval of one and ten milliseconds (for HZ = 1000)
// Allows to measure time intervals with a discrete of one and ten milliseconds.
// From the user level, functions are supported: open, close, read, vrite.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
//***************************************************************************************
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/timer.h>
#include <asm/atomic.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/spinlock_types.h>


#define KERNEL_NEW // >= 3.1.6

#ifdef KERNEL_NEW
    #define CLASS_DEV_CREATE(class, devt, device, name) device_create(class, device, devt, NULL, "%s", name)
    #define CLASS_DEV_DESTROY(class, devt) device_destroy(class, devt)
#else
    #define CLASS_DEV_CREATE(_class, _devt, _device, _name) device_create(_class, _device, _devt, _name)
    #define CLASS_DEV_DESTROY(_class, _devt) device_destroy(_class, _devt)
#endif

#define UNIT(file) MINOR(file->f_dentry->d_inode->i_rdev)

#define DevName "tmr"

#define Buf_Size 128


static struct class *tmr_class = NULL;
static unsigned char *ibuff=NULL;
static unsigned int my_msec;
static struct timer_list my_timer;
static unsigned long long varta1;
static atomic_t varta10;


static int Major = 0;
static int Device_Open = 0;
static int tim10 = 10;
static spinlock_t tmr_lock;


struct tmr_sio {
  struct cdev cdev;
};
//************************************************************
//      Timer 1 msec (for linux kernel with HZ=1000)
//************************************************************
void MyTimer(unsigned long d)
{

    spin_lock(&tmr_lock);
	varta1++;//64-bit variable, incremented by 1 every millisecond
    spin_unlock(&tmr_lock);

    tim10--;
    if (!tim10) {
	tim10 = 10;
	atomic_inc(&varta10);//32-bit variable, incremented by 1 every 10 milliseconds
    }

    my_timer.expires = jiffies + HZ/1000;
    add_timer(&my_timer);

    return;
}
//************************************************************
//                    open timer
//************************************************************
static int tmr_open(struct inode *inode, struct file *filp)
{
struct tmr_sio *sio;

  if (Device_Open) return -EBUSY;

  Device_Open++;

  sio = container_of(inode->i_cdev, struct tmr_sio, cdev);
  filp->private_data = sio;

  return 0;
}
//************************************************************
//                    close timer
//************************************************************
static int tmr_release(struct inode *inode, struct file *filp)
{

  if (Device_Open > 0) Device_Open--;

  return 0;
}
//************************************************************
//                read data from timer
//************************************************************
static ssize_t tmr_read(struct file *filp, char __user *buff, size_t count, loff_t *offp) 
{ 
ssize_t ret; 
unsigned int tim, done = 0; 
unsigned long long one_ms;

  if (count == 0) return (atomic_read(&varta10));

  ret = 0; memset(ibuff,0,8);

  switch (count) {
    case 4 : //32-bit timer reading
      ret = 4; 
      tim = atomic_read(&varta10); 
      memcpy(&ibuff[0],&tim,ret); 
      done = 1; 
    break; 
    case 8: //64-bit timer reading
      spin_lock_bh(&tmr_lock); 
	one_ms = varta1; 
      spin_unlock_bh(&tmr_lock); 
      memcpy(&ibuff[0],&one_ms,8); 
      done = 1; ret = 8; 
    break; 
  }

  if (done) 
    if(copy_to_user(buff,ibuff,ret)) { 
      printk(KERN_ALERT "\n%s: Kernel tmr_read ERROR (copy_to_user) : count=%u\n",DevName,ret); 
      return -EFAULT; 
    }

    return ret; 
}
//************************************************************
//                 write data to device
//************************************************************
static ssize_t tmr_write(struct file *filp, const char __user *buff, size_t count, loff_t *offp) 
{ 
ssize_t ret = 0;
unsigned char cmd;
unsigned int zero;

  ret = count; if (ret > Buf_Size) return -EFAULT;

  if (copy_from_user(ibuff,buff,ret)) {
    printk(KERN_ALERT "\n%s: Kernel tmr_write ERROR (copy_from_user) : count=%u\n", DevName, ret);
    return -EFAULT;
  }

  cmd = *(ibuff);

  switch (cmd) {
    case 4://reset the 10-millisecond timer to zero
      if (count != 1) return -EFAULT;//the length of the command must be 1 byte !!!
      zero = 0; atomic_set(&varta10, zero); 
      ret = 1; 
      printk(KERN_ALERT "%s: clear timer (10ms) from user\n",DevName); 
    break;
    case 8://reset a millisecond timer to zero
      if (count != 1) return -EFAULT; //the length of the command must be 1 byte !!!
      zero = 0; 
      spin_lock_bh(&tmr_lock); 
	varta1 = 0; 
      spin_unlock_bh(&tmr_lock); 
      ret = 1; 
      printk(KERN_ALERT "%s: clear timer (1ms) from user\n",DevName); 
    break;
  }

  return ret;
}
//*************************************************************
static struct file_operations tmr_fops = {
  .owner = THIS_MODULE,
  .open = tmr_open,
  .release = tmr_release,
  .read = tmr_read,
  .write = tmr_write,
};
//************************************************************
static void init_sio(struct tmr_sio *sio)
{
dev_t dev = MKDEV(Major,0);

  cdev_init(&sio->cdev, &tmr_fops);
  cdev_add(&sio->cdev, dev, 1);

}

static void deinit_sio(struct tmr_sio *sio) 
{
  cdev_del(&sio->cdev);
}

static struct tmr_sio chan_sio;

// ************************************************************
//                   init driver
// ************************************************************
static int __init tmr_init(void)
{
dev_t dev;

  if (!Major) {
    if ((alloc_chrdev_region(&dev, 0, 1, DevName)) < 0) {
      printk(KERN_ALERT "%s: Allocation device failed\n",DevName);
      return 1;
    }
    Major = MAJOR(dev);
    printk(KERN_ALERT "%s: Device allocated with major number %d\n",DevName,Major);
  } else {
    if (register_chrdev_region(MKDEV(Major,0),1,DevName)<0) {
      printk(KERN_ALERT "%s: Registration failed\n",DevName);
      return 1;
    }
    printk(KERN_ALERT "%s: Device registered with major number %d\n",DevName,Major);
  }

  init_sio(&chan_sio); 
  tmr_class = class_create(THIS_MODULE, "timer"); 
  CLASS_DEV_CREATE(tmr_class, MKDEV(Major, 0), NULL, DevName); 

//-------------------------------------------------------------------
  ibuff = kmalloc(Buf_Size,GFP_KERNEL);
  if (!ibuff) {
    printk(KERN_ALERT "%s: VM for reading buffer allocation failed\n",DevName);
    goto err_out;
  }

  spin_lock_init(&tmr_lock);

  Device_Open = 0;
  my_msec = 0; tim10 = 10; varta1 = 0;
  atomic_set(&varta10, my_msec);
  init_timer(&my_timer);
  my_timer.function = MyTimer;
  my_timer.expires = jiffies + 10;
  add_timer(&my_timer);

  printk(KERN_ALERT "%s: Start timer (1ms)\n",DevName);

  return 0; 

err_out:

  CLASS_DEV_DESTROY(tmr_class, MKDEV(Major, 0)); 
  class_destroy(tmr_class); 

  if (ibuff) kfree(ibuff);

  return -1; 
} 
//************************************************************ 
// deinit driver
//************************************************************ 
static void __exit tmr_exit(void) 
{

  del_timer(&my_timer);

  if (ibuff) kfree(ibuff);

  unregister_chrdev_region(MKDEV(Major, 0), 1);
  deinit_sio(&chan_sio);
  CLASS_DEV_DESTROY(tmr_class, MKDEV(Major, 0));
  class_destroy(tmr_class);

  printk(KERN_ALERT "%s: Unregistered timer, release memory buffer\n",DevName);

  return;
}

module_init(tmr_init);
module_exit(tmr_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SalaraSoft <a.ilminsky@gmail.com>");

