/*
   Copyright (C) 2026  Zachary Lamb

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see
   <https://www.gnu.org/licenses/>.
*/

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#define NAME "loopback"
#define MINORS 1

struct loopback_data {
  struct cdev cdev;
  struct mutex lock;
  char last;
};

static dev_t dev;
static struct loopback_data data;
static struct class *class;
static struct device *device;

static int loopback_open(struct inode *, struct file *);
static ssize_t loopback_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t loopback_write(struct file *, const char __user *, size_t,
                              loff_t *);

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = loopback_open,
    .read = loopback_read,
    .write = loopback_write,
};

static int __init loopback_init(void) {
  int err;

  err = alloc_chrdev_region(&dev, 0, MINORS, NAME);
  if (err)
    return err;

  cdev_init(&data.cdev, &fops);
  err = cdev_add(&data.cdev, dev, 1);
  if (err)
    goto out3;

  mutex_init(&data.lock);
  data.last = 0;

  class = class_create(NAME);
  if (IS_ERR(class)) {
    err = PTR_ERR(class);
    goto out2;
  }

  device = device_create(class, NULL, dev, &data, NAME);
  if (IS_ERR(device)) {
    err = PTR_ERR(device);
    goto out1;
  }

  return 0;

out1:
  class_destroy(class);
out2:
  cdev_del(&data.cdev);
out3:
  unregister_chrdev_region(dev, MINORS);
  return err;
}

static int loopback_open(struct inode *inode, struct file *file) {
  file->private_data = &data;
  return 0;
}

static ssize_t loopback_read(struct file *file, char __user *user_buffer,
                             size_t size, loff_t *offset) {
  struct loopback_data *this_data = file->private_data;
  int err;
#define BUF_LEN 64
  char c, buf[BUF_LEN];
  size_t n = size;

  err = mutex_lock_interruptible(&this_data->lock);
  if (err)
    return err;
  c = this_data->last;
  mutex_unlock(&this_data->lock);

  memset(buf, c, BUF_LEN);
  while (n) {
    size_t to_write = n > BUF_LEN ? BUF_LEN : n;
    if (copy_to_user(user_buffer, buf, to_write))
      return -EFAULT;
    user_buffer += to_write;
    n -= to_write;
  }

  return size;
}

static ssize_t loopback_write(struct file *file, const char __user *user_buffer,
                              size_t size, loff_t *offset) {
  struct loopback_data *this_data = file->private_data;
  int err;
  char c;

  if (!size)
    return 0;

  if (get_user(c, user_buffer + (size - 1)))
    return -EFAULT;

  err = mutex_lock_interruptible(&this_data->lock);
  if (err)
    return err;
  this_data->last = c;
  mutex_unlock(&this_data->lock);

  return size;
}

static void __exit loopback_exit(void) {
  device_destroy(class, dev);
  class_destroy(class);
  cdev_del(&data.cdev);
  unregister_chrdev_region(dev, MINORS);
}

module_init(loopback_init);
module_exit(loopback_exit);

MODULE_AUTHOR("Zachary Lamb");
MODULE_DESCRIPTION("Provides a loopback character device that always reads the "
                   "last written byte.");
MODULE_LICENSE("GPL");