#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/ktime.h>
#include <linux/time.h>

MODULE_AUTHOR("Markuze Alex amarkuze@vmware.com");
MODULE_DESCRIPTION("Simple stream logger");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.1");

static unsigned int bufsize __read_mostly = 4096;
MODULE_PARM_DESC(bufsize, "Log buffer size in KB (4096)");
module_param(bufsize, uint, 0);

static const char debugfsnem[] = "data_path_log";

static struct {
	spinlock_t	lock;
	wait_queue_head_t wait;
	ktime_t		start;
	u32		flush;

	unsigned long	head, tail;
} dp_logger;

static inline int dp_logger_used(void)
{
	return (dp_logger.head != dp_logger.tail);
}

static int tcpprobe_open(struct inode *inode, struct file *file)
{
	/* Reset (empty) log */
	spin_lock_bh(&dp_logger.lock);
	dp_logger.head = dp_logger.tail = 0;
	dp_logger.start = ktime_get();
	spin_unlock_bh(&dp_logger.lock);

	return 0;
}

static int tcpprobe_sprint(char *tbuf, int n)
{
	const struct tcp_log *p
		= dp_logger.log + dp_logger.tail;
	struct timespec64 ts
		= ktime_to_timespec64(ktime_sub(p->tstamp, dp_logger.start));

	return scnprintf(tbuf, n,
			"%lu.%09lu %pISpc %pISpc %d %#x %#x %u %u %u %u %u\n",
			(unsigned long)ts.tv_sec,
			(unsigned long)ts.tv_nsec,
			&p->src, &p->dst, p->length, p->snd_nxt, p->snd_una,
			p->snd_cwnd, p->ssthresh, p->snd_wnd, p->srtt, p->rcv_wnd);
}

static ssize_t tcpprobe_write(struct file *file, const char __user *buf,
			      size_t len, loff_t *ppos)
{
		dp_logger.flush = 1;	
		wake_up(&dp_logger.wait);
		return len;
}

static ssize_t tcpprobe_read(struct file *file, char __user *buf,
			     size_t len, loff_t *ppos)
{
	int error = 0;
	size_t cnt = 0;

	if (!buf)
		return -EINVAL;

	dp_logger.flush = 0;

	while (cnt < len) {
		char tbuf[256];
		int width;

		/* Wait for data in buffer */
		error = wait_event_interruptible(dp_logger.wait,
						 dp_logger.flush || dp_logger_used() > 0);
		if (error || dp_logger.flush)
			break;

		spin_lock_bh(&dp_logger.lock);
		if (dp_logger.head == dp_logger.tail) {
			/* multiple readers race? */
			spin_unlock_bh(&dp_logger.lock);
			continue;
		}

		width = tcpprobe_sprint(tbuf, sizeof(tbuf));

		if (cnt + width < len)
			dp_logger.tail = (dp_logger.tail + 1) & (bufsize - 1);

		spin_unlock_bh(&dp_logger.lock);

		/* if record greater than space available
		   return partial buffer (so far) */
		if (cnt + width >= len)
			break;

		if (copy_to_user(buf + cnt, tbuf, width))
			return -EFAULT;
		cnt += width;
	}

	return cnt == 0 ? error : cnt;
}

static const struct file_operations tcpprobe_fops = {
	.owner	 = THIS_MODULE,
	.open	 = tcpprobe_open,
	.read    = tcpprobe_read,
	.write   = tcpprobe_write,
	.llseek  = noop_llseek,
};

static __init int tcpprobe_init(void)
{
	int ret = -ENOMEM;

	/* Warning: if the function signature of tcp_rcv_established,
	 * has been changed, you also have to change the signature of
	 * jtcp_rcv_established, otherwise you end up right here!
	 */
	BUILD_BUG_ON(__same_type(tcp_rcv_established,
				 jtcp_rcv_established) == 0);

	init_waitqueue_head(&dp_logger.wait);
	spin_lock_init(&dp_logger.lock);

	if (bufsize == 0)
		return -EINVAL;

	bufsize = roundup_pow_of_two(bufsize);
	dp_logger.log = kcalloc(bufsize, sizeof(struct tcp_log), GFP_KERNEL);
	if (!dp_logger.log)
		goto err0;

	if (!proc_create(procname, S_IRUSR, init_net.proc_net, &tcpprobe_fops))
		goto err0;

	ret = register_jprobe(&tcp_jprobe);
	if (ret)
		goto err1;

	pr_info("probe registered (port=%d/fwmark=%u) bufsize=%u\n",
		port, fwmark, bufsize);
	return 0;
 err1:
	remove_proc_entry(procname, init_net.proc_net);
 err0:
	kfree(dp_logger.log);
	return ret;
}
module_init(tcpprobe_init);

static __exit void tcpprobe_exit(void)
{
	remove_proc_entry(procname, init_net.proc_net);
	unregister_jprobe(&tcp_jprobe);
	kfree(dp_logger.log);
}
module_exit(tcpprobe_exit);
