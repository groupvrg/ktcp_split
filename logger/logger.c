#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/module.h>
#include <linux/ktime.h>
#include <linux/time.h>

#include "dpb.h"
#include "stats_cb_mgr.h"

MODULE_AUTHOR("Markuze Alex amarkuze@vmware.com");
MODULE_DESCRIPTION("Simple stream logger");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");

static unsigned int bufsize __read_mostly = 4096;
MODULE_PARM_DESC(bufsize, "Log buffer size in KB (4096)");
module_param(bufsize, uint, 0);

#define stats_procname	"stats"
#define log_procname	"log"
#define PROC_DIR 	"dp_logger"

static struct {
	struct trvl_buffer_mgr 	stats;
	struct stats_cb_mgr	cbm;
} dp_logger;

#define _sbm &dp_logger.stats
#define _s_cbm &dp_logger.cbm

/****************DEBUG**********************/

int debug_stats(void *ctx, char *buffer, int tailroom)
{
	return scnprintf(buffer, tailroom, "Hello World\n");
}

static struct stats_cb_entry debug_entry = {
	.ctx = NULL,
	.get_stat = debug_stats,
};
/*******************************************/
static int stats_log_open(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t logger_log_write(struct file *file, const char __user *buf,
			      size_t size, loff_t *ppos)
{
	char *kbuf;
	/* start by dragging the command into memory */
	if (size <= 1 || size >= PAGE_SIZE)
		return -EINVAL;

	kbuf = memdup_user_nul(buf, size);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	trvlb_log_formated_string(_sbm, kbuf, size);
	kfree(kbuf);

	return size;
}

static ssize_t stats_log_write(struct file *file, const char __user *buf,
			      size_t size, loff_t *ppos)
{
	scbm_register(_s_cbm, &debug_entry);
	return size;
}

static ssize_t stats_log_read(struct file *file, char __user *buf,
			      size_t len, loff_t *ppos)
{
	size_t cnt = 0;

	if (!buf)
		return -EINVAL;

	/*Stats: will always print something,
	 * cat seems to call read again if prev call was != 0
	 * ppos points to file->f_pos*/
	if ((*ppos)++ & 1)
		return 0;

	pr_err("%s called\n", __FUNCTION__);
	scbm_collect_stats(_s_cbm, _sbm);

	while (cnt < len) {
		int size;
		char *buffer;

		buffer = trvlb_pull_formated_buffer(_sbm, &size);
		pr_err("size is %d\n", size);
		if (!buffer || !size)
			return cnt;
		
		if (cnt + size > len) {
			trvlb_put_formated_buffer(_sbm, buffer, 0);
			return cnt;
		}

		if (copy_to_user(buf + cnt, buffer, size)) {
			trvlb_put_formated_buffer(_sbm, buffer, 0);
			return -EFAULT;
		}

		/* Consume buffer from the logging system */
		trvlb_put_formated_buffer(_sbm, buffer, size);
		cnt += size;
	}
	return cnt;
}

static const struct file_operations logger_log_fops = {
	.owner	 = THIS_MODULE,
	.open	 = stats_log_open,
	.read    = stats_log_read,
	.write   = logger_log_write,
	.llseek  = noop_llseek,
};

static const struct file_operations stats_log_fops = {
	.owner	 = THIS_MODULE,
	.open	 = stats_log_open,
	.read    = stats_log_read,
	.write   = stats_log_write,
	.llseek  = noop_llseek,
};

struct proc_dir_entry *proc_dir;

static inline int md_init(void)
{
	int ret = 0;
	bufsize = roundup_pow_of_two(bufsize);
	trvlb_init(_sbm);
	scbm_init(_s_cbm);

	return ret;
}

static inline void md_close(void)
{
	scbm_close(_s_cbm);
	trvlb_close(_sbm);
}

static __init int dp_log_init(void)
{
	int ret = -ENOMEM;

	if (bufsize == 0)
		return -EINVAL;

	if ((ret = md_init()))
		goto err;
	if (! (proc_dir = proc_mkdir_mode(PROC_DIR, 00555, NULL)))
		goto err_proc;
	if (!proc_create(stats_procname, 0666, proc_dir, &stats_log_fops))
		goto err_proc2;
	if (!proc_create(log_procname, 0666, proc_dir, &logger_log_fops))
		goto err_proc2;

	return 0;
err_proc2:
	remove_proc_subtree(PROC_DIR, NULL);
err_proc:
	md_close();
err:
	return ret;
}
module_init(dp_log_init);

static __exit void dp_log_exit(void)
{
	remove_proc_subtree(PROC_DIR, NULL);
	md_close();
}
module_exit(dp_log_exit);
