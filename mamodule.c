#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/jiffies.h>

#define DEV_NAME "my_device"
#define CLASS_NAME "my_class"
#define PROC_DIR "my_proc_dir"
#define PROC_STATS "my_proc"

static char *buffer;
static size_t buffer_size = 1024;
static int open_count;
static int write_count;
static int read_count;
static long open_time;
static long total_time;
static int major;
static struct class *my_class;
static struct device *my_device;


static struct proc_dir_entry *proc_dir, *proc_stats;

static ssize_t virtfile_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{   
	int i;
	char *tmp_buf;

	if (*off >= buffer_size)
		return 0;

	if (len > buffer_size - *off)
		len = buffer_size - *off;

	tmp_buf = kmalloc(len, GFP_KERNEL);
	if (!tmp_buf)
		return -ENOMEM;


	//##############################################
	// attention ici
	for (i = 0; i < len; i++)
		tmp_buf[i] = buffer[*off + i] ^ 0x55;

	//###############################################

	if (copy_to_user(buf, tmp_buf, len)) {
		kfree(tmp_buf);
		return -EFAULT;
	}

	kfree(tmp_buf);
	*off += len;
	read_count++;

	return len;
}


static ssize_t virtfile_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{
	int i;
	char *tmp_buf;

	if (*off >= buffer_size)
		return -ENOSPC;

	if (len > buffer_size - *off)
		len = buffer_size - *off;

	tmp_buf = kmalloc(len, GFP_KERNEL);
	if (!tmp_buf)
		return -ENOMEM;

	if (copy_from_user(tmp_buf, buf, len)) {
		kfree(tmp_buf);
		return -EFAULT;
	}

	//##############################################
	// attention ici
	for (i = 0; i < len; i++)
		buffer[*off + i] = tmp_buf[i] ^ 0x55;
	//##############################################

	kfree(tmp_buf);
	*off += len;
	write_count++;

	return len;
}

static int virtfile_open(struct inode *inode, struct file *filp)
{   
	open_count++;
	open_time = jiffies;

	return 0;
}


static int virtfile_release(struct inode *inode, struct file *filp)
{   
	open_time = jiffies - open_time;
	total_time = total_time + open_time;

	return 0;
}


static const struct file_operations virtfile_fops = {
	.owner = THIS_MODULE,
	.open = virtfile_open,
	.release = virtfile_release,
	.read = virtfile_read,
	.write = virtfile_write,
};


static int virtfile_stats_show(struct seq_file *m, void *v)
{
	seq_printf(m, "Open count: %d\n", open_count);
	seq_printf(m, "Write count: %d\n", write_count);
	seq_printf(m, "Read count: %d\n", read_count);
	seq_printf(m, "Open time: %ld millisecondes\n", jiffies_to_msecs(total_time));

	return 0;
}


static int virtfile_stats_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, virtfile_stats_show, NULL);
}


static const struct proc_ops virtfile_stats_fops = {
	.proc_open = virtfile_stats_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int __init virtfile_init(void)
{
    printk(KERN_INFO "my_device: init\n");

	major = register_chrdev(0, DEV_NAME, &virtfile_fops);
	if (major < 0) {
        printk(KERN_ALERT "my_device: failed to register a major number\n");
		kfree(buffer);
		return major;
	}
    my_class = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(my_class)) {
		unregister_chrdev(major, DEV_NAME);
		printk(KERN_ALERT "my_device: failed to register device class\n");
		return PTR_ERR(my_class);
	}
    my_device = device_create(my_class, NULL, MKDEV(major, 0), NULL, DEV_NAME);
	if (IS_ERR(my_device)) {
		class_destroy(my_class);
		unregister_chrdev(major, DEV_NAME);
		printk(KERN_ALERT "my_device: failed to create the device\n");
		return PTR_ERR(my_device);
	}

    buffer = kmalloc(buffer_size, GFP_KERNEL);
	if (!buffer) {
		device_destroy(my_class, MKDEV(major, 0));
		class_destroy(my_class);
		unregister_chrdev(major, DEV_NAME);
		printk(KERN_ALERT "my_device: failed to allocate memory\n");
		return -ENOMEM;
	}

	proc_dir = proc_mkdir(PROC_DIR, NULL);
	if (!proc_dir) {
		unregister_chrdev(major, DEV_NAME);
		kfree(buffer);
		return -ENOMEM;
	}

	proc_stats = proc_create(PROC_STATS, 0, proc_dir, &virtfile_stats_fops);
	if (!proc_stats) {
		remove_proc_entry(PROC_DIR, NULL);
		unregister_chrdev(major, DEV_NAME);
		kfree(buffer);
        printk(KERN_INFO "my_proc: fail\n");
		return -ENOMEM;
	}
    printk(KERN_INFO "my_proc: is ok\n");

	return 0;
}



static void __exit virtfile_exit(void)
{
	remove_proc_entry(PROC_STATS, proc_dir);
	remove_proc_entry(PROC_DIR, NULL);
    device_destroy(my_class, MKDEV(major, 0));
    class_destroy(my_class);
	unregister_chrdev(0, DEV_NAME);
	kfree(buffer);
    printk(KERN_INFO "my_dev: exit\n");
}



module_init(virtfile_init);
module_exit(virtfile_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("hakuna matata");
MODULE_DESCRIPTION("Virtual File Driver");