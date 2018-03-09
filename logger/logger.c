#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/module.h>
#include <linux/ktime.h>
#include <linux/time.h>

#include "dpb.h"

MODULE_AUTHOR("Markuze Alex amarkuze@vmware.com");
MODULE_DESCRIPTION("Simple stream logger");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");

static unsigned int bufsize __read_mostly = 4096;
MODULE_PARM_DESC(bufsize, "Log buffer size in KB (4096)");
module_param(bufsize, uint, 0);

#define procname	"data_path_log"
#define PROC_DIR 	"dp_logger"

static struct {
	struct dp_buffer_mgr mgr;
} dp_logger;

#define _dpbm &dp_logger.mgr

static int dplog_open(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t dplog_write(struct file *file, const char __user *buf,
			      size_t size, loff_t *ppos)
{
	char *kbuf;
	/* start by dragging the command into memory */
	if (size <= 1 || size >= PAGE_SIZE)
		return -EINVAL;

	kbuf = memdup_user_nul(buf, size);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	dpb_log_formated_string(_dpbm, kbuf, size);
	kfree(kbuf);

	return size;
}

static ssize_t dplog_read(struct file *file, char __user *buf,
			     size_t len, loff_t *ppos)
{
	size_t cnt = 0;

	if (!buf)
		return -EINVAL;

	while (cnt < len) {
		int size;
		char *buffer;

		buffer = dpb_pull_formated_buffer(_dpbm, &size);
		if (!buffer || !size)
			return cnt;
		
		if (cnt + size > len) {
			dpb_put_formated_buffer(_dpbm, buffer, 0);
			return cnt;
		}

		if (copy_to_user(buf + cnt, buffer, size)) {
			dpb_put_formated_buffer(_dpbm, buffer, 0);
			return -EFAULT;
		}

		/* Consume buffer from the logging system */
		dpb_put_formated_buffer(_dpbm, buffer, size);
		cnt += size;
	}
	return cnt;
}

static const struct file_operations dplog_fops = {
	.owner	 = THIS_MODULE,
	.open	 = dplog_open,
	.read    = dplog_read,
	.write   = dplog_write,
	.llseek  = noop_llseek,
};

struct proc_dir_entry *proc_dir;

static __init int dplog_init(void)
{
	int ret = -ENOMEM;

	if (bufsize == 0)
		return -EINVAL;

	bufsize = roundup_pow_of_two(bufsize);
	if ((ret = dpb_init(_dpbm, bufsize)))
		goto err;
	if (! (proc_dir = proc_mkdir_mode(PROC_DIR, 00555, NULL)))
		goto err_proc;
	if (!proc_create(procname, 0666, proc_dir, &dplog_fops))
		goto err_proc2;

	return 0;
err_proc2:
	remove_proc_subtree(PROC_DIR, NULL);
err_proc:
	dpb_close(_dpbm);
err:
	return ret;
}
module_init(dplog_init);

static __exit void dplog_exit(void)
{
	remove_proc_subtree(PROC_DIR, NULL);
	dpb_close(_dpbm);
}
module_exit(dplog_exit);
