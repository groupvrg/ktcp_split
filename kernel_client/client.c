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
#include <linux/tcp.h>

#include <net/net_namespace.h> //init_net
#include <net/sock.h> //ZCOPY
#include <net/tcp.h> //mss
#include <uapi/linux/in.h> //sockaddr_in

#include <linux/uaccess.h>
#include <linux/cpumask.h>

//Thread pool API
//#include "../tcpsplit/thread_pool.h"

MODULE_AUTHOR("Markuze Alex markuze@cs.technion.ac.il");
MODULE_DESCRIPTION("Deferred I/O client");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");

#define POLLER_DIR_NAME "io_client"
#define procname "client"

typedef void (*bind_mask_func)(struct task_struct *, const struct cpumask *);
bind_mask_func pkthread_bind_mask;

//static struct kthread_pool thread_pool = {.pool_size = 128 };
static struct proc_dir_entry *proc_dir;
static struct task_struct *udp_client_task;
static struct task_struct *tcp_client_task;
static struct task_struct *tcp_server[4];
struct kmem_cache *pairs;
static int is_zcopy;

struct sock_pair {
	struct socket *in;
	struct socket *out;
	struct wait_queue_head wait;
};

static ssize_t client_write(struct file *file, const char __user *buf,
                              size_t len, loff_t *ppos)
{
	char *kbuf = memdup_user_nul(buf, len);
	if (IS_ERR_OR_NULL(kbuf))
		return PTR_ERR(kbuf);

	is_zcopy = 0;
	if (kbuf[0] == '0') {
		is_zcopy = 1;
		trace_printk("zero copy set...\n");
	} else
		trace_printk("Input... %c\n", kbuf[0]);
	kfree(kbuf);

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
#if 0
static inline void put_page_v(struct page *page)
{
	page = compound_head(page);
	trace_printk("put %p[%d]\n", page, page_count(page));

	if (put_page_testzero(page))
		__put_page(page);
}
#endif

int half_duplex(struct socket *in, struct socket *out)
{
        struct kvec kvec[VEC_SZ];
        int id = 0, i;
        int rc;
        uint64_t bytes = 0;

        rc = -ENOMEM;
        for (i = 0; i < VEC_SZ; i++) {
                kvec[i].iov_len = PAGE_SIZE;
                /*TODO: In case of alloc failure put_qp is needed */
                if (! (kvec[i].iov_base = page_address(alloc_page(GFP_KERNEL))))
                        goto err;
        }
        do {
                struct msghdr msg = { 0 };
                if ((rc = kernel_recvmsg(in, &msg, kvec, VEC_SZ, (PAGE_SIZE * VEC_SZ), 0)) <= 0) {
                        TRACE_DEBUG("%s [%s] (%d) at %s with %lld bytes", __FUNCTION__,
                                        dir  ? "TX" : "RX", rc, id ? "Send" : "Rcv", bytes);
			sock_shutdown(out);
			goto err;
		}
		TRACE_PRINT("%s [%s] %s :  %d", __FUNCTION__,
				dir  ? "TX" : "RX", id ? "Send" : "Rcv", rc);


		bytes += rc;
		id ^= 1;
		msg.msg_flags   |= MSG_ZEROCOPY;

		//FIXME: Need to make sure we know num of frags
		if ((rc = kernel_sendmsg(out, &msg, kvec,
					get_kvec_len(kvec, VEC_SZ), rc)) <= 0) {
			TRACE_PRINT("ERROR: %s [%s] (%d) at %s with %lld bytes", __FUNCTION__,
					rc, id ? "Send" : "Rcv", bytes);
			sock_shutdown(in);
			goto err;
		}
		id ^= 1;

	} while (!kthread_should_stop());

err:
	for (i = 0; i < VEC_SZ; i++)
                free_page((unsigned long)(kvec[i].iov_base));
	trace_printk("%s [%s] stopping on error (%d) at %s with %lld bytes\n", __FUNCTION__,
			rc, id ? "Send" : "Rcv", bytes);
	return rc;
}

int half_duplex_zero(struct socket *in, struct socket *out)
{
	struct kvec kvec[VEC_SZ];
	int rc;
	uint64_t bytes = 0;


	sock_set_flag(sock->tx->sk, SOCK_KERN_ZEROCOPY);
	do {
		struct msghdr msg = { 0 };

		memset(kvec, 0, sizeof(kvec));

		if ((rc = tcp_read_sock_zcopy_blocking(in, kvec, VEC_SZ)) <= 0) {
			TRACE_DEBUG("ERROR: %s [%s] (%d) at %s with %lld bytes", __FUNCTION__,
					rc, id ? "Send" : "Rcv", bytes);
			sock_shutdown(out);
			goto err;
		}
		TRACE_PRINT("%s [%s] %s :  %d", __FUNCTION__,
				dir  ? "TX" : "RX", id ? "Send" : "Rcv", rc);


		bytes += rc;
		id ^= 1;
		msg.msg_flags   |= MSG_ZEROCOPY;

		//FIXME: Need to make sure we know num of frags
		if ((rc = kernel_sendmsg(sock->tx, &msg, kvec,
					get_kvec_len(kvec, VEC_SZ), rc)) <= 0) {
			TRACE_PRINT("ERROR: %s [%s] (%d) at %s with %lld bytes", __FUNCTION__,
					rc, id ? "Send" : "Rcv", bytes);
			sock_shutdown("");
			goto err;
		}
		id ^= 1;

	} while (!kthread_should_stop());

err:
	trace_printk("%s [%s] stopping on error (%d) at %s with %lld bytes\n", __FUNCTION__,
			rc, id ? "Send" : "Rcv", bytes);
	return rc;
}
//TODO: Consider a recvmsg server on a different port
//TODO: same for sender, just dont set the ZEROCOPY Flag
#define VEC_SZ 16

static int tcp_read_sock_zcopy_blocking(struct socket *sock, struct kvec *pages_array, unsigned int nr_pages)
{
	struct sock *sk = sock->sk;
	struct sk_buff *last = NULL;
	long timeo = 1 * HZ;//MAX_SCHEDULE_TIMEOUT;
	int rc;

retry:
	last = skb_peek_tail(&sk->sk_receive_queue);
	if (!last)
		goto wait;

	if ((rc = tcp_read_sock_zcopy(sock, pages_array, nr_pages)) < 0) {
		//trace_printk("Error %d\n", rc);
		goto out;
	}
	if (!rc) {
wait:
		lock_sock(sk);
		rc = sk_wait_data(sock->sk, &timeo, NULL);
		last = skb_peek_tail(&sk->sk_receive_queue);
		release_sock(sk);
		goto retry;
	}
out:
	return rc;
}

#define RX_ORDER	2
static int start_new_connection(void *nsock)
{
	struct socket *sock = nsock;
	struct kvec kvec[VEC_SZ];
	unsigned long bytes = 0 ;
	ktime_t start = ktime_get();
	//ktime_t initial_start = start;
	int i, rc = 0;

	for (i = 0; i < VEC_SZ; i++) {
		kvec[i].iov_len = PAGE_SIZE << RX_ORDER;
		if (! (kvec[i].iov_base = page_address(alloc_pages(GFP_KERNEL, RX_ORDER))))
			goto out;
	}

	while (!kthread_should_stop()) {
		struct msghdr msg = { 0 };
		ktime_t now = ktime_get();
		//ktime_get_seconds
		if (unlikely(ktime_after(now, ktime_add(start, NSEC_PER_SEC)))) {
			trace_printk("%llu) %lu\n", ktime_sub(now, start), bytes);
			start = now;
			bytes = 0;
		}
		if ((rc = kernel_recvmsg(sock, &msg, kvec, VEC_SZ, ((PAGE_SIZE << RX_ORDER) * VEC_SZ), 0)) <= 0) {
			trace_printk("Error %d\n", rc);
			goto out;
		}
		//trace_printk("RC = %d\n", rc);
		bytes += rc;
	}
out:
	for (i = 0; i < VEC_SZ; i++)
		free_pages((unsigned long)(kvec[i].iov_base), RX_ORDER);

	trace_printk("C ya, cowboy...\n");
	return rc;
}

static int start_new_connection_z(void *nsock)
{
	struct socket *sock = nsock;
	struct kvec kvec[VEC_SZ];
	unsigned long bytes = 0 ;
	ktime_t start = ktime_get();
	//ktime_t initial_start = start;
	int i, rc = 0;

	for (i = 0; i < VEC_SZ; i++) {
		kvec[i].iov_len = PAGE_SIZE;
		kvec[i].iov_base = NULL;
		//if (! (kvec[i].iov_base = page_address(alloc_page(GFP_KERNEL))))
		//	goto out;
	}

	while (!kthread_should_stop()) {
		ktime_t now = ktime_get();
		//ktime_get_seconds
		if (unlikely(ktime_after(now, ktime_add(start, NSEC_PER_SEC)))) {
			trace_printk("%llu) %lu\n", ktime_sub(now, start), bytes);
			start = now;
			bytes = 0;
		}
		//if ((rc = kernel_recvmsg(sock, &msg, kvec, VEC_SZ, (PAGE_SIZE * VEC_SZ), 0)) <= 0) {
		if ((rc = tcp_read_sock_zcopy_blocking(sock, kvec, VEC_SZ)) < 0) {
			trace_printk("Error %d\n", rc);
			goto out;
		}
		if (!rc) {
			/* Adopt sk_wait_data*/
			//schedule();
			continue;
		}
		//trace_printk("RC = %d\n", rc);
		bytes += rc;
		for (i = 0; i < VEC_SZ; i++) {
			if (!kvec[i].iov_base)
				break;
			//trace_printk("Freeing %p [%lu]",virt_to_head_page(kvec[i].iov_base), kvec[i].iov_len);
			put_page(virt_to_page(kvec[i].iov_base));
			kvec[i].iov_base = NULL;
		}

	}
out:
#if 0
	for (i = 0; i < VEC_SZ; i++)
		free_page((unsigned long)(kvec[i].iov_base));
#endif
	trace_printk("C ya, cowboy...\n");
	return rc;
}

static int proxy_out(void *arg)
{
#define PORT	8080
//#define SERVER_ADDR (10<<24|1<<16|4<<8|38) /*10.1.4.38*/
#define SERVER_ADDR (10<<24|154<<16|0<<8|21) /*10.154.0.21*/
	struct sock_pair *pair = arg;
	int rc, i = 0;
	unsigned long cnt, max;
	struct socket *tx = NULL;
	struct sockaddr_in srv_addr = {0};

        srv_addr.sin_family             = AF_INET;
        srv_addr.sin_addr.s_addr        = htonl(SERVER_ADDR);
        srv_addr.sin_port               = htons(PORT);

	if ((rc = sock_create_kern(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP, &tx))) {
                trace_printk("RC = %d (%d)\n", rc, __LINE__);
		goto err;
        }

        if ((rc = kernel_connect(tx, (struct sockaddr *)&srv_addr, sizeof(struct sockaddr), 0))) {
                trace_printk("RC = %d (%d)\n", rc, __LINE__);
		goto err;
        }
	wake_up(&pair->wait);
	return 0;
err:
	trace_printk("Failed to connect (%d)\n", rc);
	return -1
}

static int proxy_in(void *arg)
{
	struct sock_pair *pair = arg;
	int error = wait_event_interruptible_timeout(pair->wait,
							pair->out, HZ);
	if (error)
		goto err;
	//kernel_sock_shutdown(net->socket, SHUT_RDWR);
	//sock_release(net->socket);
	half_duplex(pair);
	return 0;
err:
	trace_printk("Waiting timed out (%d)\n", error);
	return -1
}

#define K_PROXY 8083
static int proxy_server(void *unused)
{
	int rc = 0;
	struct socket *sock = NULL;
	struct sockaddr_in srv_addr;

	if ((rc = sock_create_kern(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP, &sock)))
		goto error;

	srv_addr.sin_family 		= AF_INET;
	srv_addr.sin_addr.s_addr 	= htonl(INADDR_ANY);
	srv_addr.sin_port 		= htons(K_PROXY);

	if ((rc = kernel_bind(sock, (struct sockaddr *)&srv_addr, sizeof(srv_addr))))
		goto error;

	if ((rc = kernel_listen(sock, 32)))
		goto error;

	trace_printk("accepting on port %d\n", PORT_Z);
	do {
		struct sock_pair *pair = kmem_cache_alloc(pairs, GFP_KERNEL);;
		struct socket *nsock;

		rc = kernel_accept(sock, &nsock, 0);
		if (unlikely(rc))
			goto out;

		pair->in = nsock;
		init_waitqueue_head(&pair->wait);

		kthread_run(proxy_in, pair, "proxy_in_%lx", (unsigned long)nsock);
		kthread_run(proxy_out, pair, "proxy_out_%lx", (unsigned long)nsock);

	} while (!kthread_should_stop());

error:
	trace_printk("Exiting %d\n", rc);
out:
	if (sock)
		sock_release(sock);
	return rc;
}

#define PORT_Z 8082
static int split_server_z(void *unused)
{
	int rc = 0;
	struct socket *sock = NULL;
	struct sockaddr_in srv_addr;

	if ((rc = sock_create_kern(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP, &sock)))
		goto error;

	srv_addr.sin_family 		= AF_INET;
	srv_addr.sin_addr.s_addr 	= htonl(INADDR_ANY);
	srv_addr.sin_port 		= htons(PORT_Z);

	if ((rc = kernel_bind(sock, (struct sockaddr *)&srv_addr, sizeof(srv_addr))))
		goto error;

	if ((rc = kernel_listen(sock, 32)))
		goto error;

	trace_printk("accepting on port %d\n", PORT_Z);
	do {
		struct socket *nsock;

		rc = kernel_accept(sock, &nsock, 0);
		if (unlikely(rc))
			goto out;

		kthread_run(start_new_connection_z, nsock, "server_z_%lx", (unsigned long)nsock);

	} while (!kthread_should_stop());

error:
	trace_printk("Exiting %d\n", rc);
out:
	if (sock)
		sock_release(sock);
	return rc;
}

#define PORT	8081
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

		kthread_run(start_new_connection, nsock, "server_%lx", (unsigned long)nsock);

	} while (!kthread_should_stop());

error:
	trace_printk("Exiting %d\n", rc);
out:
	if (sock)
		sock_release(sock);
	return rc;
}


static inline void send_loop(struct socket *tx, struct msghdr *msg, struct kvec *vec)
{
	int rc, i = 0;

	for (i = 0; i < (1<<19); i++) {
		struct kvec kvec[16];

		memcpy(kvec, vec, sizeof(struct kvec) << 4);

		while ((rc = trace_sendmsg(tx, msg, kvec, 16, (PAGE_SIZE << 6))) <= 0) {
			trace_printk("Received an Err %d\n", rc);
			schedule();
		}
	}
}

static inline void tcp_client(void)
{
#define PORT	8080
//#define SERVER_ADDR (10<<24|1<<16|4<<8|38) /*10.1.4.38*/
#define SERVER_ADDR (10<<24|154<<16|0<<8|21) /*10.154.0.21*/
	int rc, i = 0;
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

	if (! (base  = page_address(alloc_pages(GFP_KERNEL|__GFP_COMP, 6)))) {
		rc = -ENOMEM;
		goto err;
	}
 #define virt_to_pfn(kaddr) (__pa(kaddr) >> PAGE_SHIFT)
	for (i = 0; i < 16; i++) {
		kvec[i].iov_len = PAGE_SIZE << 2;
		kvec[i].iov_base = base + (i * (PAGE_SIZE << 2));
		trace_printk("Page %d) 0x%lx (0x%lx)\n",i, (unsigned long)(kvec[i].iov_base), (unsigned long)virt_to_page(kvec[i].iov_base));
	}

	if ((rc = sock_create_kern(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP, &tx))) {
                trace_printk("RC = %d (%d)\n", rc, __LINE__);
		goto err;
        }

	if (is_zcopy) {
		sock_set_flag(tx->sk, SOCK_KERN_ZEROCOPY);
		msg.msg_flags 	|= MSG_ZEROCOPY;
	}

        if ((rc = kernel_connect(tx, (struct sockaddr *)&srv_addr, sizeof(struct sockaddr), 0))) {
                trace_printk("RC = %d (%d)\n", rc, __LINE__);
		goto err;
        }
	trace_printk("Connected, sending...\n");
	send_loop(tx, &msg, kvec);
	trace_printk("Hello messages sent.(%d) mss %u\n", i, tcp_current_mss(tx->sk));
	goto ok;
err:
	trace_printk("ERROR %d\n", rc);
ok:
	sock_release(tx);
	free_pages((unsigned long)base, 6);
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
	//cbn_kthread_pool_init(&thread_pool);
	proc_dir = proc_mkdir_mode(POLLER_DIR_NAME, 00555, NULL);
	pkthread_bind_mask = (void *)kallsyms_lookup_name("kthread_bind_mask");

	qp_slab = kmem_cache_create("cbn_qp_mdata",
					sizeof(struct sock_pair), 0, 0, NULL);

	udp_client_task  = kthread_create(poll_thread, ((void *)0), "udp_client_thread");
	tcp_client_task  = kthread_create(poll_thread, ((void *)1), "tcp_client_thread");

	//pkthread_bind_mask(client_task, cpumask_of(0));

	udp_client_task->flags &= ~PF_NO_SETAFFINITY;
	tcp_client_task->flags &= ~PF_NO_SETAFFINITY;

	wake_up_process(udp_client_task);
	wake_up_process(tcp_client_task);

	tcp_server[0] = kthread_run(split_server, NULL, "server_thread");
	tcp_server[1] = kthread_run(split_server_z, NULL, "server_thread_z");
	tcp_server[3] = kthread_run(split_server_z, NULL, "proxy_thread");
	tcp_server[4] = kthread_run(split_server_z, NULL, "proxy_thread_z");

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
	kthread_stop(tcp_server[0]);
	kthread_stop(tcp_server[1]);
	kthread_stop(tcp_server[2]);
	kthread_stop(tcp_server[3]);
}

module_init(client_init);
module_exit(client_exit);
