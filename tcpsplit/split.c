#include <linux/init.h>      // included for __init and __exit macros
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/netdevice.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <net/sock.h>  //sock->to
#include "tcp_split.h"
#include "pool.h"
#include "cbn_common.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Markuze Alex");
MODULE_DESCRIPTION("CBN TCP Split Module");

static unsigned int cbn_ingress_hook(void *priv,
					struct sk_buff *skb,
					const struct nf_hook_state *state)
{
	trace_iph(skb, priv);

	return NF_ACCEPT;
}

static struct nf_hook_ops cbn_nf_hooks[] = {
			{
			.hook		= cbn_ingress_hook,
			.hooknum	= NF_INET_POST_ROUTING,
			.pf		= PF_INET,
			.priority	= NF_IP_PRI_FIRST,
			.priv		= "TX"
			},
			{
			.hook		= cbn_ingress_hook,
			.hooknum	= NF_INET_PRE_ROUTING,
			.pf		= PF_INET,
			.priority	= NF_IP_PRI_FIRST,
			.priv		= "RX"
			},
//TODO: Add LOCAL_IN to mark packets with tennant_id
};

#define SERVER_PORT (9 << 10) // 9K // _1oo1
#define BACKLOG     512

static struct kthread_pool cbn_pool = {.pool_size = DEF_CBN_POOL_SIZE};

static struct task_struct *server_task;
static struct list_head task_list;
static struct kmem_cache *qp_slab;

struct cbn_qp {
	struct socket *tx;
	struct socket *rx;
};

static inline void stop_proxies(void)
{

		//kernel_sock_shutdown(sock, SHUT_RDWR);
		//sock_release(sock);
	cbn_kthread_pool_clean(&cbn_pool);
}

#define INIT_TRACE	char ___buff[512] = {0}; int ___idx = 0;

#define TRACE_LINE {	 pr_err("%d:%s\n", __LINE__, current->comm);___idx += sprintf(&___buff[___idx], "\n\t\t%s:%d", __FUNCTION__, __LINE__); }
#define TRACE_PRINT(fmt, ...)
#ifndef TRACE_PRINT
#define TRACE_PRINT(fmt, ...) { trace_printk("%d:%s:"fmt"\n", __LINE__, current->comm,##__VA_ARGS__ );\
				/*pr_err("%d:%s:"fmt"\n", __LINE__, current->comm,##__VA_ARGS__ ); */\
				/* ___idx += sprintf(&___buff[___idx], "\n\t\t%s:%d:"fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__); */}
#endif
#define DUMP_TRACE	 if (___idx) {___buff[___idx] = '\n'; trace_printk(___buff);} ___buff[0] = ___idx = 0;

static int half_duplex(void *arg)
{
	struct cbn_qp *qp = arg;
	struct kvec kvec;
	int rc = -ENOMEM;

	INIT_TRACE
	if (! (kvec.iov_base = page_address(alloc_page(GFP_KERNEL))))
		goto err;

	kvec.iov_len = PAGE_SIZE;

	do {
		struct msghdr msg = { 0 };
		TRACE_PRINT("waiting for bytes...");
		if ((rc = kernel_recvmsg(qp->rx, &msg, &kvec, 1, PAGE_SIZE, 0)) <= 0)
			goto err;
		TRACE_PRINT("Received %d bytes", rc);
		//use kern_sendpage if flags needed.
		if ((rc = kernel_sendmsg(qp->tx, &msg, &kvec, 1, rc)) <= 0)
				goto err;
		TRACE_PRINT("Sent %d bytes", rc);
	} while (!kthread_should_stop());
	goto out;
err:
	TRACE_PRINT("%s sleeping on error (%d)\n", __FUNCTION__, rc);
	set_current_state(TASK_INTERRUPTIBLE);
	if (!kthread_should_stop())
		schedule();
	__set_current_state(TASK_RUNNING);
out:
	TRACE_PRINT("%s going out\n", __FUNCTION__);
	free_page((unsigned long)(kvec.iov_base));
	DUMP_TRACE
	return rc;
}

static int start_new_connection(void *arg)
{
	int rc, size, line;
//	int T = 1;
	struct socket *rx = arg;
	struct sockaddr_in cli_addr;
	struct socket *tx;
	struct sockaddr_in addr;
	struct cbn_qp *qp;
	struct cbn_qp lqp;
	struct pool_elem *elem;

	INIT_TRACE

	size = sizeof(addr);
	line = __LINE__;
	if ((rc = kernel_getsockopt(rx, SOL_IP, SO_ORIGINAL_DST, (char *)&addr, &size)))
		goto create_fail;

	line = __LINE__;
	if ((rc = kernel_getpeername(rx, (struct sockaddr *)&cli_addr, &size)))
		goto create_fail;

	line = __LINE__;
	if ((rc = sock_create_kern(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP, &tx)))
		goto create_fail;
/*
	line = __LINE__;
	if ((rc = kernel_setsockopt(tx, SOL_IP, IP_TRANSPARENT, (char *)&T, sizeof(int))))
		goto connect_fail;

	TRACE_PRINT("connection from port %d IP %pI4n (%d)", ntohs(cli_addr.sin_port), &cli_addr.sin_addr, cli_addr.sin_family);
	line = __LINE__;
	if ((rc = kernel_bind(tx, (struct sockaddr *)&cli_addr, sizeof(cli_addr))))
		goto connect_fail;
*/
	TRACE_PRINT("connecting remote port %d IP %pI4n (%d)", ntohs(addr.sin_port), &addr.sin_addr, addr.sin_family);
	line = __LINE__;
	if ((rc = kernel_connect(tx, (struct sockaddr *)&addr, sizeof(addr), 0)))
		goto connect_fail;

	line = __LINE__;
	if ((rc = kernel_getsockname(tx, (struct sockaddr *)&addr, &size)))
		goto connect_fail;
	TRACE_PRINT("connected local port %d IP %pI4n (%d)", ntohs(addr.sin_port), &addr.sin_addr, addr.sin_family);

	qp = kmem_cache_alloc(qp_slab, GFP_KERNEL);
	qp->rx = lqp.tx = tx;
	qp->tx = lqp.rx = rx;

	elem = kthread_pool_run(&cbn_pool, half_duplex, qp);
	DUMP_TRACE
	half_duplex(&lqp);

	TRACE_PRINT("closing port %d IP %pI4n", ntohs(addr.sin_port), &addr.sin_addr);
	/* Teardown */
	kthread_stop(elem->task); //TODO: dont like the elem-> ..., fix teh API when paralel connect is on

	/* TX partner stopped - free qp*/
	kmem_cache_free(qp_slab, qp);

	/* free both sockets*/
	rc = line = 0;
connect_fail:
	sock_release(tx);
create_fail:
	sock_release(rx);
	TRACE_PRINT("out [%d - %d]", rc, ++line);
	DUMP_TRACE
	return rc;
}

static int split_server(void *unused)
{
	int rc = 0;
	struct socket *sock;
	struct sockaddr_in srv_addr;
	INIT_TRACE

	if ((rc = sock_create_kern(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP, &sock)))
		goto out;

	//kernel_sock_ioctl(, FIONBIO,)
	srv_addr.sin_family 		= AF_INET;
	srv_addr.sin_addr.s_addr 	= htonl(INADDR_ANY);
	srv_addr.sin_port 		= htons(SERVER_PORT);

	if ((rc = kernel_bind(sock, (struct sockaddr *)&srv_addr, sizeof(srv_addr))))
		goto bind_failed;

	if ((rc = kernel_listen(sock, BACKLOG)))
		goto listen_failed;
	TRACE_PRINT("waiting for a connection...\n");
loop:
	do {
		struct socket *nsock;
		//struct pool_elem *elem; TODO: nothing to do with elem right now - will need on para-connect

		sock->sk->sk_rcvtimeo = 1 * HZ;
		rc = kernel_accept(sock, &nsock, 0); //O_NONBLOCK for non blocking new socket
		if (rc == -EAGAIN)
			goto loop;
		if (unlikely(rc))
			goto accept_failed;

		TRACE_PRINT("starting new connection...\n");
		kthread_pool_run(&cbn_pool, start_new_connection, nsock);

	} while (!kthread_should_stop());

accept_failed:
listen_failed:
bind_failed:
	TRACE_PRINT("Exiting");
	sock_release(sock);
out:
	DUMP_TRACE
	return rc;
}

int __init cbn_datapath_init(void)
{
	server_task = kthread_run(split_server, NULL, "cbn_tcp_split_server");
	INIT_LIST_HEAD(&task_list);
	qp_slab = kmem_cache_create("cbn_qp_mdata",
					sizeof(struct cbn_qp), 0, 0, NULL);

	cbn_kthread_pool_init(&cbn_pool);
	//nf_register_hooks(cbn_nf_hooks, ARRAY_SIZE(cbn_nf_hooks));
	return 0;
}

void __exit cbn_datapath_clean(void)
{
	pr_err("stopping server_task\n");
	kthread_stop(server_task);
	pr_err("server_task stopped stopping stop_proxies\n");
	stop_proxies();
	pr_err("proxies stopped\n");
	//nf_unregister_hooks(cbn_nf_hooks,  ARRAY_SIZE(cbn_nf_hooks));
}

module_init(cbn_datapath_init);
module_exit(cbn_datapath_clean);

