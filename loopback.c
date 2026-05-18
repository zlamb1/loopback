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

#include <asm/page.h>

#include <linux/init.h>
#include <linux/minmax.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#define BUF_LEN 128
#define BUFFERED(data) ((data)->mode_data.buffered)
#define CAP (PAGE_SIZE * 2)
#define NAME "loopback"
#define MODE_SET_IOCTL 1
#define SINGLE(data) ((data)->mode_data.single)

enum loopback_mode {
  MODE_SINGLE = 1,
  MODE_BUFFERED = 2,
};

union loopback_mode_data {
  char single;
  struct {
    size_t len;
    char *data;
  } buffered;
};

struct loopback_data {
  struct miscdevice mdev;
  struct mutex lock;

  enum loopback_mode mode;
  union loopback_mode_data mode_data;
};

static struct loopback_data data;

static int loopback_open(struct inode *, struct file *);
static ssize_t loopback_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t loopback_write(struct file *, const char __user *, size_t,
                              loff_t *);
static long loopback_ioctl(struct file *, unsigned int, unsigned long);

static const struct file_operations fops = {
    .open = loopback_open,
    .owner = THIS_MODULE,
    .read = loopback_read,
    .unlocked_ioctl = loopback_ioctl,
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
  data.mode = MODE_SINGLE;
  SINGLE(&data) = 0;

  err = misc_register(&data.mdev);
  if (err)
    return err;

  return 0;
}

static void loopback_mode_free(struct loopback_data *this_data) {
  switch (this_data->mode) {
  case MODE_SINGLE:
    break;
  case MODE_BUFFERED:
    kfree(BUFFERED(this_data).data);
    break;
  }
}

static int loopback_open(struct inode *inode, struct file *file) {
  file->private_data = &data;
  return 0;
}

static ssize_t loopback_read(struct file *file, char __user *user_buffer,
                             size_t size, loff_t *offset) {
  struct loopback_data *this_data = file->private_data;
  int err;
  size_t n = size;

  if (!size)
    return 0;

  if ((err = mutex_lock_interruptible(&this_data->lock)))
    return err;

  switch (this_data->mode) {
  case MODE_SINGLE: {
    char buf[BUF_LEN];
    char c = SINGLE(this_data);
    mutex_unlock(&this_data->lock);
    memset(buf, c, BUF_LEN);
    while (n) {
      size_t to_write = min(n, BUF_LEN);
      if (copy_to_user(user_buffer, buf, to_write))
        return -EFAULT;
      user_buffer += to_write;
      n -= to_write;
    }
    break;
  }
  case MODE_BUFFERED: {
    size_t len = BUFFERED(this_data).len, to_write;
    char *buf = BUFFERED(this_data).data;
    if (*offset >= len) {
      err = 0;
      goto out;
    }
    buf += *offset;
    len -= *offset;
    to_write = min(size, len);
    if (copy_to_user(user_buffer, buf, to_write)) {
      err = -EFAULT;
      goto out;
    }
    mutex_unlock(&this_data->lock);
    *offset += to_write;
    return to_write;
  }
  }

  return size;

out:
  mutex_unlock(&this_data->lock);
  return err;
}

static long loopback_ioctl(struct file *file, unsigned int cmd,
                           unsigned long arg) {
  if (cmd == MODE_SET_IOCTL) {
    struct loopback_data *this_data = file->private_data;
    int err;

    if (mutex_lock_interruptible(&this_data->lock))
      return -ERESTARTSYS;

    if (this_data->mode == arg) {
      err = 0;
      goto out;
    }

    switch ((enum loopback_mode)arg) {
    case MODE_SINGLE:
      loopback_mode_free(this_data);
      SINGLE(this_data) = 0;
      break;
    case MODE_BUFFERED: {
      char *new_data = kmalloc(CAP, GFP_KERNEL);
      if (!new_data) {
        err = -ENOMEM;
        goto out;
      }
      loopback_mode_free(this_data);
      BUFFERED(this_data).len = 0;
      BUFFERED(this_data).data = new_data;
      break;
    }
    default:
      err = -EINVAL;
      goto out;
    }

    this_data->mode = arg;
    mutex_unlock(&this_data->lock);
    return 0;

  out:
    mutex_unlock(&this_data->lock);
    return err;
  }

  return -ENOTTY;
}

static ssize_t loopback_write(struct file *file, const char __user *user_buffer,
                              size_t size, loff_t *offset) {
  struct loopback_data *this_data = file->private_data;
  int err = 0;

  if (!size)
    return 0;

  err = mutex_lock_interruptible(&this_data->lock);
  if (err)
    return err;
  switch (this_data->mode) {
  case MODE_SINGLE: {
    char c;
    if (get_user(c, user_buffer + (size - 1))) {
      err = -EFAULT;
      break;
    }
    SINGLE(this_data) = c;
    break;
  }
  case MODE_BUFFERED: {
    // Offset isn't considered.
    // Any write will overwrite the
    // currently buffered contents.
    if (size > CAP) {
      // Cap how much is written so we don't
      // overly allocate in kernel space.
      err = -EFBIG;
      break;
    }
    if (copy_from_user(BUFFERED(this_data).data, user_buffer, size)) {
      // We don't know what got clobbered.
      // Could be fixed by copying into a temp buffer first.
      BUFFERED(this_data).len = 0;
      err = -EFAULT;
      break;
    }
    BUFFERED(this_data).len = size;
    *offset = 0;
    break;
  }
  }
  mutex_unlock(&this_data->lock);

  if (err)
    return err;

  return size;
}

static void __exit loopback_exit(void) {
  misc_deregister(&data.mdev);
  loopback_mode_free(&data);
}

module_init(loopback_init);
module_exit(loopback_exit);

MODULE_AUTHOR("Zachary Lamb");
MODULE_DESCRIPTION("Provides a multimodal character device that provides "
                   "single byte or buffered loopback functionality.");
MODULE_LICENSE("GPL");