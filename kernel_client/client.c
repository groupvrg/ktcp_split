#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/net.h>
#include <linux/kallsyms.h>
#include <linux/cpumask.h>

#include <net/net_namespace.h> //init_net
#include <uapi/linux/in.h> //sockaddr_in

#include <linux/uaccess.h>
#include <linux/cpumask.h>

MODULE_AUTHOR("Markuze Alex markuze@cs.technion.ac.il");
MODULE_DESCRIPTION("Deferred I/O client");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");

#define POLLER_DIR_NAME "io_client"
#define procname "client"

typedef void (*bind_mask_func)(struct task_struct *, const struct cpumask *);
bind_mask_func pkthread_bind_mask;

static struct proc_dir_entry *proc_dir;
static struct task_struct *udp_client_task;
static struct task_struct *tcp_client_task;

static ssize_t client_write(struct file *file, const char __user *buf,
                              size_t len, loff_t *ppos)
{
	trace_printk("%s\n", __FUNCTION__);
	wake_up_process(file->private_data);
	return len;
}

static ssize_t client_read(struct file *file, char __user *buf,
                             size_t buflen, loff_t *ppos)
{
	if (!buf)
		return -EINVAL;

        /* Stats: will always print something,
         * cat seems to call read again if prev call was != 0
         * ppos points to file->f_pos */
        if ((*ppos)++ & 1)
                return 0;

#if 0
	for_each_online_cpu(cpu) {
		char line[256];
		int len;

		len = snprintf(line, 256, "online cpu %d\n", cpu);
		if (buflen <= cnt + len + 50 * (TOP/STEP) ) {
			trace_printk("<%d>check failed...\n", cpu);
			break;
		}
		cnt += len;
		copy_to_user(buf + cnt, line, len);

		trace_printk("online cpu %d\n", cpu);
		out |= dump_per_core_trace(cpu, &cnt, buf, buflen);
	}
#endif
	return 0;
}

static int noop_open(struct inode *inode, struct file *file)
{
	file->private_data = PDE_DATA(inode);
	trace_printk("%s\n", __FUNCTION__);
	return 0;
}

static inline void tcp_client(void)
{
#define PORT	8080
//#define SERVER_ADDR (10<<24|1<<16|4<<8|38) /*10.1.4.38*/
#define SERVER_ADDR (10<<24|154<<16|0<<8|21) /*10.154.0.21*/
	int rc, i = 0;
	struct socket *tx = NULL;
	struct sockaddr_in srv_addr = {0};
	struct msghdr msg = { 0 };
	struct kvec kvec[16];
	void *base = NULL;

        srv_addr.sin_family             = AF_INET;
        srv_addr.sin_addr.s_addr        = htonl(SERVER_ADDR);
        srv_addr.sin_port               = htons(PORT);

	msg.msg_name = &srv_addr;
	msg.msg_namelen = sizeof(struct sockaddr);

	if (! (base  = page_address(alloc_pages(GFP_KERNEL, 4)))) {
		rc = -ENOMEM;
		goto err;
	}

	for (i = 0; i < 16; i++) {
		kvec[i].iov_len = PAGE_SIZE;
		kvec[i].iov_base = base + (i * PAGE_SIZE);
	}

	if ((rc = sock_create_kern(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP, &tx))) {
		goto err;
        }

        if ((rc = kernel_connect(tx, (struct sockaddr *)&srv_addr, sizeof(struct sockaddr), 0))) {
                trace_printk("RC = %d (%d)", rc, __LINE__);
		goto err;
        }
	trace_printk("Connected, sending...\n");

	for (i = 0; i < (1<<19); i++) {
	      kernel_sendmsg(tx, &msg, kvec, 16, (16 << PAGE_SHIFT));
	}
	trace_printk("Hello messages sent.(%d)\n", i);
	goto ok;
err:
	trace_printk("ERROR %d\n", rc);
ok:
	sock_release(tx);
	free_pages((unsigned long)base, 4);
	return;
}

static inline void udp_client(void)
{
	int rc, i = 0;
	struct socket *tx = NULL;
	struct sockaddr_in srv_addr = {0};
	struct msghdr msg = { 0 };
	struct kvec kvec;

        srv_addr.sin_family             = AF_INET;
        srv_addr.sin_addr.s_addr        = htonl(SERVER_ADDR);
        srv_addr.sin_port               = htons(PORT);

	msg.msg_name = &srv_addr;
	msg.msg_namelen = sizeof(struct sockaddr);

	kvec.iov_len = PAGE_SIZE;
	if (! (kvec.iov_base = page_address(alloc_page(GFP_KERNEL)))) {
		rc = -ENOMEM;
		goto err;
	}

	if ((rc = sock_create_kern(&init_net, PF_INET, SOCK_DGRAM, IPPROTO_UDP, &tx))) {
		goto err;
        }

	snprintf(kvec.iov_base, 64, "HELLO!");
	for (i = 0; i < (1<<25); i++) {
	      kernel_sendmsg(tx, &msg, &kvec, 1, 42);
	}
	trace_printk("Hello message sent.(%d)\n", i);
	return;
err:
	trace_printk("ERROR %d\n", rc);
	return;
}

static int poll_thread(void *data)
{
	trace_printk("starting new send...\n");
	while (!kthread_should_stop()) {

		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
		__set_current_state(TASK_RUNNING);

		if (!kthread_should_stop()) {
			trace_printk("Task %p\n", data);
			tcp_client();
		}
	}
	return 0;
}

static const struct file_operations client_fops = {
	.owner	= THIS_MODULE,
	.open	= noop_open,
	.read	= client_read,
	.write	= client_write,
	.llseek	= noop_llseek,
};

static __init int client_init(void)
{
	proc_dir = proc_mkdir_mode(POLLER_DIR_NAME, 00555, NULL);
	pkthread_bind_mask = (void *)kallsyms_lookup_name("kthread_bind_mask");

	udp_client_task  = kthread_create(poll_thread, 0, "udp_client_thread");
	tcp_client_task  = kthread_create(poll_thread, 1, "tcp_client_thread");

	//pkthread_bind_mask(client_task, cpumask_of(0));

	udp_client_task->flags &= ~PF_NO_SETAFFINITY;
	tcp_client_task->flags &= ~PF_NO_SETAFFINITY;

	wake_up_process(udp_client_task);
	wake_up_process(tcp_client_task);

	if (!proc_create_data("udp_"procname, 0666, proc_dir, &client_fops, udp_client_task))
		goto err;
	if (!proc_create_data("tcp_"procname, 0666, proc_dir, &client_fops, tcp_client_task))
		goto err;

	trace_printk("TCP: %p\nUDP: %p\n", udp_client_task, tcp_client_task);
	return 0;
err:
	return -1;
}

static __exit void client_exit(void)
{
	remove_proc_subtree(POLLER_DIR_NAME, NULL);
	kthread_stop(udp_client_task);
	kthread_stop(tcp_client_task);
}

module_init(client_init);
module_exit(client_exit);
