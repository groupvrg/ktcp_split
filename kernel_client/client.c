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
#include <net/sock.h> //ZCOPY
#include <uapi/linux/in.h> //sockaddr_in

#include <linux/uaccess.h>
#include <linux/cpumask.h>

//Thread pool API
#include "../tcpsplit/thread_pool.h"

MODULE_AUTHOR("Markuze Alex markuze@cs.technion.ac.il");
MODULE_DESCRIPTION("Deferred I/O client");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");

#define POLLER_DIR_NAME "io_client"
#define procname "client"

typedef void (*bind_mask_func)(struct task_struct *, const struct cpumask *);
bind_mask_func pkthread_bind_mask;

static struct proc_dir_entry *proc_dir;
static struct kthread_pool thread_pool = {.pool_size = 128 };
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

//TODO: Consider a recvmsg server on a different port
//TODO: same for sender, just dont set the ZEROCOPY Flag
static int start_new_connection(void *)
	//page * array
	while (!kthread_should_stop()) {
		//receive: report every sec: time_after, HZ
	}
}

#define PORT	8080
static int split_server(void *unused)
{
	int rc = 0;
	struct socket *sock = NULL;
	struct sockaddr_in srv_addr;

	if ((rc = sock_create_kern(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP, &sock)))
		goto error;

	srv_addr.sin_family 		= AF_INET;
	srv_addr.sin_addr.s_addr 	= htonl(INADDR_ANY);
	srv_addr.sin_port 		= htons(PORT);

	if ((rc = kernel_bind(sock, (struct sockaddr *)&srv_addr, sizeof(srv_addr))))
		goto error;

	if ((rc = kernel_listen(sock, 32)))
		goto error;

	trace_printk("accepting on port %d\n", PORT);
	do {
		struct socket *nsock;

		rc = kernel_accept(sock, &nsock, 0);
		if (unlikely(rc))
			goto out;

		kthread_pool_run(&cbn_pool, start_new_connection, nsock);

	} while (!kthread_should_stop());

error:
	trace_printk("Exiting %d <%d>\n", rc, (server) ? server->status : -1);
out:
	if (sock)
		sock_release(sock);
	return rc;
}

/*
	TODO: Need a thread to do accept (look at ktcp)
	TODO: Create a thread pool user...
	TODO: integrate DAMN, KTCP magazines
*/
static void tcp_zcopy_rx(struct socket *sock, struct page **pages_array, unsigned int nr_pages)
{
	struct sock *sk = sock->sk;
	const skb_frag_t *frags;
	u32 seq, len, offset, nr = 0;
	struct tcp_sock *tp;
	struct sk_buff *skb;
	int rc = -ENOTCONN;

	lock_sock(sk);

	if (sk->sk_state == TCP_LISTEN)
		goto out;

	sock_rps_record_flow(sk);

	tp = tcp_sk(sk);
	seq = tp->copied_seq;
#if 0
	unsigned long size = nr_pages << PAGE_SHIFT;

	/* We dont actually care, accept everything...*/
	if (tcp_inq(sk) < size) {
		ret = sock_flag(sk, SOCK_DONE) ? -EIO : -EAGAIN;
		goto out;
	}
	/* Abort if urgent data is in the area  --- Hmm.....*/
	if (unlikely(tp->urg_data)) {
		u32 urg_offset = tp->urg_seq - seq;

		ret = -EINVAL;
		if (urg_offset < size)
			goto out;
	}
#endif
	skb = tcp_recv_skb(sk, seq, &offset);
	ret = -EINVAL;
skb_start:
	offset -= skb_headlen(skb);
	/* Linear data present... - Handle it or Fix virtio */
	if ((int)offset < 0)
		goto out;
	/* frag list present ? eehnmmm... gro wtf?*/
	if (skb_has_frag_list(skb))
		goto out;
	len = skb->data_len - offset;
	frags = skb_shinfo(skb)->frags;
	while (offset) {
		if (frags->size > offset)
			goto out;
		offset -= frags->size;
		frags++;
	}
	while (nr < nr_pages) {
		if (len) {
			if (len < PAGE_SIZE)
				goto out;
			if (frags->size != PAGE_SIZE || frags->page_offset)
				goto out;
			pages_array[nr++] = skb_frag_page(frags);
			frags++;
			len -= PAGE_SIZE;
			seq += PAGE_SIZE;
			continue;
		}
		skb = skb->next;
		offset = seq - TCP_SKB_CB(skb)->seq;
		goto skb_start;
	}
#if 0
	/* Ok now we need to get these pages...*/
	for (nr = 0; nr < nr_pages; nr++) {
		ret = vm_insert_page(vma, vma->vm_start + (nr << PAGE_SHIFT),
				     pages_array[nr]);
		if (ret)
			goto out;
	}
#endif
	/* operation is complete, we can 'consume' all skbs */
	tp->copied_seq = seq;
	tcp_rcv_space_adjust(sk);

	/* Clean up data we have read: This will do ACK frames. */
	tcp_recv_skb(sk, seq, &offset);
	tcp_cleanup_rbuf(sk, size);

	ret = 0;
out:
	release_sock(sk);

	return ret;
}

static inline void tcp_client(void)
{
#define PORT	8080
//#define SERVER_ADDR (10<<24|1<<16|4<<8|38) /*10.1.4.38*/
#define SERVER_ADDR (10<<24|154<<16|0<<8|21) /*10.154.0.21*/
	int rc, i = 0, j;
	unsigned long cnt, max;
	struct socket *tx = NULL;
	struct sockaddr_in srv_addr = {0};
	struct msghdr msg = { 0 };
	struct kvec kvec[16];
	void *base = NULL;

	cnt = max = 0;
        srv_addr.sin_family             = AF_INET;
        srv_addr.sin_addr.s_addr        = htonl(SERVER_ADDR);
        srv_addr.sin_port               = htons(PORT);

	msg.msg_name 	= &srv_addr;
	msg.msg_namelen = sizeof(struct sockaddr);
	msg.msg_flags 	|= MSG_ZEROCOPY;

	if (! (base  = page_address(alloc_pages(GFP_KERNEL, 4)))) {
		rc = -ENOMEM;
		goto err;
	}
 #define virt_to_pfn(kaddr) (__pa(kaddr) >> PAGE_SHIFT)
	for (i = 0; i < 16; i++) {
		kvec[i].iov_len = PAGE_SIZE;
		kvec[i].iov_base = base + (i * PAGE_SIZE);
		trace_printk("Page %d) 0x%lx (%lu)\n",i, (unsigned long)(kvec[i].iov_base), virt_to_pfn(kvec[i].iov_base));
	}

	if ((rc = sock_create_kern(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP, &tx))) {
                trace_printk("RC = %d (%d)\n", rc, __LINE__);
		goto err;
        }

	sock_set_flag(tx->sk, SOCK_KERN_ZEROCOPY);

        if ((rc = kernel_connect(tx, (struct sockaddr *)&srv_addr, sizeof(struct sockaddr), 0))) {
                trace_printk("RC = %d (%d)\n", rc, __LINE__);
		goto err;
        }
	trace_printk("Connected, sending...\n");

	for (i = 0; i < (1<<19); i++) {
	//	for (j = 0; j < 16; j++) {
	//		get_page(virt_to_page(base + (i * PAGE_SIZE)));
	//	}


		while ((rc = kernel_sendmsg(tx, &msg, kvec, 16, (16 << PAGE_SHIFT))) <= 0) {
			//trace_printk("Received an Err %d\n", rc);
			rc = 0;
			cnt++;
		}
		if (unlikely(cnt > max)) {
			trace_printk("Polling for %lu [%lu]\n", cnt, max);
			max = cnt;
		}
		cnt = 0;
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
	cbn_kthread_pool_init(&thread_pool);
	proc_dir = proc_mkdir_mode(POLLER_DIR_NAME, 00555, NULL);
	pkthread_bind_mask = (void *)kallsyms_lookup_name("kthread_bind_mask");

	udp_client_task  = kthread_create(poll_thread, ((void *)0), "udp_client_thread");
	tcp_client_task  = kthread_create(poll_thread, ((void *)1), "tcp_client_thread");

	//pkthread_bind_mask(client_task, cpumask_of(0));

	udp_client_task->flags &= ~PF_NO_SETAFFINITY;
	tcp_client_task->flags &= ~PF_NO_SETAFFINITY;

	wake_up_process(udp_client_task);
	wake_up_process(tcp_client_task);

	kthread_pool_run(&thread_pool, split_server, NULL);

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
