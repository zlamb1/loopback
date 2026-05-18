/**
 * Copyright (C) 2026  Zachary Lamb
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see
 * <https://www.gnu.org/licenses/>.
 *
 **/

#include <linux/init.h>
#include <linux/minmax.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#define BUF_LEN 128
#define NAME "loopback"

struct loopback_data {
  struct miscdevice mdev;
  struct mutex lock;
  char last;
};

static struct loopback_data data;

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

  data.mdev = (struct miscdevice){
      .fops = &fops,
      .name = NAME,
      .minor = MISC_DYNAMIC_MINOR,
  };
  mutex_init(&data.lock);
  data.last = 0;

  err = misc_register(&data.mdev);
  if (err)
    return err;

  return 0;
}

static int loopback_open(struct inode *inode, struct file *file) {
  file->private_data = &data;
  return 0;
}

static ssize_t loopback_read(struct file *file, char __user *user_buffer,
                             size_t size, loff_t *offset) {
  struct loopback_data *this_data = file->private_data;
  int err;
  char c, buf[BUF_LEN];
  size_t n = size;

  if (!size)
    return 0;

  err = mutex_lock_interruptible(&this_data->lock);
  if (err)
    return err;
  c = this_data->last;
  mutex_unlock(&this_data->lock);

  memset(buf, c, BUF_LEN);
  while (n) {
    size_t to_write = min(n, BUF_LEN);
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

static void __exit loopback_exit(void) { misc_deregister(&data.mdev); }

module_init(loopback_init);
module_exit(loopback_exit);

MODULE_AUTHOR("Zachary Lamb");
MODULE_DESCRIPTION("Provides a loopback character device that always reads the "
                   "last written byte.");
MODULE_LICENSE("GPL");