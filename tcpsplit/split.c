#include <linux/init.h>      // included for __init and __exit macros
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/netdevice.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <net/sock.h>  //sock->to
#include "tcp_split.h"
#include "pool.h"
#include "rb_data_tree.h"
#include "cbn_common.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Markuze Alex");
MODULE_DESCRIPTION("CBN TCP Split Module");

#define SERVER_PORT (9 << 10) // 9K // _1oo1
#define BACKLOG     512

struct sockets {
	struct socket *rx;
	struct socket *tx;
};

struct addresses {
	struct sockaddr_in dest;
	struct sockaddr_in src;
};

static struct kthread_pool cbn_pool = {.pool_size = DEF_CBN_POOL_SIZE};

//per_tennat
static struct task_struct *server_task;
static struct rb_root qp_root = RB_ROOT;
//per_tennat

static struct list_head task_list;
static struct kmem_cache *qp_slab;
static struct kmem_cache *syn_slab;

static int start_new_connection_syn(void *arg);

static unsigned int cbn_ingress_hook(void *priv,
					struct sk_buff *skb,
					const struct nf_hook_state *state)
{
	if (trace_iph(skb, priv)) {
		struct iphdr *iphdr = ip_hdr(skb);
		struct tcphdr *tcphdr = (struct tcphdr *)skb_transport_header(skb);
		struct addresses *addresses;

		//if (!strcmp(priv, "LIN"))
			goto out;
		pr_err("schedule connection %d\n", skb->mark);
		addresses = kmem_cache_alloc(syn_slab, GFP_ATOMIC);
		if (unlikely(!addresses)) {
			pr_err("Faield to alloc mem %s\n", __FUNCTION__);
			goto out;
		}
		addresses->dest.sin_addr.s_addr	= iphdr->daddr;
		addresses->src.sin_addr.s_addr	= iphdr->saddr;
		addresses->dest.sin_port	= tcphdr->dest;
		addresses->src.sin_port		= tcphdr->source;
		kthread_pool_run(&cbn_pool, start_new_connection_syn, addresses); //elem?
		//1.alloc task + data
		//2.rb_tree lookup
		// sched poll on qp init - play with niceness?
	}

out:
	return NF_ACCEPT;
}

#define CBN_PRIO_OFFSET 50

static struct nf_hook_ops cbn_nf_hooks[] = {
		{
		.hook		= cbn_ingress_hook,
		.hooknum	= NF_INET_POST_ROUTING,
		.pf		= PF_INET,
		.priority	= NF_IP_PRI_FIRST,
		.priv		= "TX"
		},
//		{
//		.hook		= cbn_ingress_hook,
//		.hooknum	= NF_INET_LOCAL_OUT,
//		.pf		= PF_INET,
//		.priority	= NF_IP_PRI_FIRST,
//		.priv		= "NF_INET_LOCAL_OUT"
//		},
//
		{
		.hook		= cbn_ingress_hook,
		.hooknum	= NF_INET_FORWARD,
		.pf		= PF_INET,
		.priority	= NF_IP_PRI_FIRST,
		.priv		= "NF_INET_LOCAL_OUT"
		},
		{
		.hook		= cbn_ingress_hook,
		.hooknum	= NF_INET_LOCAL_IN,
		.pf		= PF_INET,
		.priority	= NF_IP_PRI_FIRST,
		.priv		= "LIN"
		},
		{
		.hook		= cbn_ingress_hook,
		.hooknum	= NF_INET_PRE_ROUTING,
		.pf		= PF_INET,
		.priority	= NF_IP_PRI_RAW + CBN_PRIO_OFFSET,
		.priv		= "RX"
		},
//TODO: Add LOCAL_IN to mark packets with tennant_id
};

static inline void stop_proxies(void)
{

		//kernel_sock_shutdown(sock, SHUT_RDWR);
		//sock_release(sock);
	struct cbn_qp *pos, *tmp;
	rbtree_postorder_for_each_entry_safe(pos, tmp, &qp_root, node){
		if (pos->tx) {
			pr_err("releasing %p\n", pos->tx);
			sock_release(pos->tx);
		}
		if (pos->rx) {
			pr_err("releasing %p\n", pos->rx);
			sock_release(pos->rx);
		}
	}
	cbn_kthread_pool_clean(&cbn_pool);
}

static int half_duplex(void *arg)
{
	struct sockets *qp = arg;
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

static int start_new_connection_syn(void *arg)
{
	int rc, line;
	struct addresses *addresses = arg;
	struct cbn_qp *qp, *tx_qp;
	struct sockets sockets;
	struct socket *tx;

	INIT_TRACE

	line = __LINE__;
	TRACE_PRINT("connection to port %s [%d]", __FUNCTION__, __LINE__);
	if ((rc = sock_create_kern(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP, &tx)))
		goto create_fail;

	TRACE_PRINT("connection to port %s [%d]", __FUNCTION__, __LINE__);
	addresses->dest.sin_family = AF_INET;
	if ((rc = kernel_connect(tx, (struct sockaddr *)&addresses->dest, sizeof(struct sockaddr), 0)))
		goto connect_fail;

	qp = kmem_cache_alloc(qp_slab, GFP_KERNEL);
	qp->addr_d = addresses->dest.sin_addr;
	qp->port_s = addresses->src.sin_port;
	qp->port_d = addresses->dest.sin_port;
	qp->addr_s = addresses->src.sin_addr;

	TRACE_PRINT("connection to port %d IP %pI4n", ntohs(qp->port_d), &qp->addr_d);
	qp->tx = tx;
	qp->rx = NULL;

	//TODO: add locks to this shit
	if ((tx_qp = add_rb_data(&qp_root, qp))) { //this means the other conenction is already up
		tx_qp->tx = tx;
		kmem_cache_free(qp_slab, qp);
		qp = tx_qp;
	} else {
		while (!qp->rx) {
			if (kthread_should_stop())
				goto create_fail;
			schedule();
		}
	}
	DUMP_TRACE
	sockets.tx = qp->rx;
	sockets.rx = qp->tx;
	TRACE_PRINT("starting half duplex");
	half_duplex(&sockets);
	goto create_fail;


connect_fail:
	TRACE_PRINT("OUT: connection to port %s ", __FUNCTION__);
	sock_release(tx);
create_fail:
	TRACE_PRINT("OUT: connection to port %s ", __FUNCTION__);
	DUMP_TRACE
	return rc;
}

static int start_new_connection(void *arg)
{
	int rc, size, line;
//	int T = 1;
	struct socket *rx = arg;
	struct sockaddr_in cli_addr;
	struct sockaddr_in addr;
	struct cbn_qp *qp, *tx_qp;
	struct sockets sockets;

	INIT_TRACE

	size = sizeof(addr);
	line = __LINE__;
	if ((rc = kernel_getsockopt(rx, SOL_IP, SO_ORIGINAL_DST, (char *)&addr, &size)))
		goto create_fail;

	line = __LINE__;
	if ((rc = kernel_getpeername(rx, (struct sockaddr *)&cli_addr, &size)))
		goto create_fail;

	TRACE_PRINT("connection from port %d IP %pI4n (%d)", ntohs(cli_addr.sin_port), &cli_addr.sin_addr, cli_addr.sin_family);
/*
	line = __LINE__;
	if ((rc = sock_create_kern(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP, &tx)))
		goto create_fail;
	line = __LINE__;
	if ((rc = kernel_setsockopt(tx, SOL_IP, IP_TRANSPARENT, (char *)&T, sizeof(int))))
		goto connect_fail;

	TRACE_PRINT("connection from port %d IP %pI4n (%d)", ntohs(cli_addr.sin_port), &cli_addr.sin_addr, cli_addr.sin_family);
	line = __LINE__;
	if ((rc = kernel_bind(tx, (struct sockaddr *)&cli_addr, sizeof(cli_addr))))
		goto connect_fail;
	TRACE_PRINT("connecting remote port %d IP %pI4n (%d)", ntohs(addr.sin_port), &addr.sin_addr, addr.sin_family);
	line = __LINE__;
	if ((rc = kernel_connect(tx, (struct sockaddr *)&addr, sizeof(addr), 0)))
		goto connect_fail;

	line = __LINE__;
	if ((rc = kernel_getsockname(tx, (struct sockaddr *)&addr, &size)))
		goto connect_fail;
	TRACE_PRINT("connected local port %d IP %pI4n (%d)", ntohs(addr.sin_port), &addr.sin_addr, addr.sin_family);

*/
	qp = kmem_cache_alloc(qp_slab, GFP_KERNEL);
	qp->addr_s = addr.sin_addr;
	qp->port_d = cli_addr.sin_port;
	qp->port_s = addr.sin_port;
	qp->addr_d = cli_addr.sin_addr;
	qp->rx = rx;
	qp->tx = NULL;


	//elem = kthread_pool_run(&cbn_pool, half_duplex, qp);
	if ((tx_qp = add_rb_data(&qp_root, qp))) { //this means the other conenction is already up
		tx_qp->rx = rx;
		kmem_cache_free(qp_slab, qp);
		qp = tx_qp;
	} else {
		while (!qp->tx) {
			schedule();
		}
	}
	DUMP_TRACE
	sockets.tx = qp->tx;
	sockets.rx = qp->rx;
	half_duplex(&sockets);

	TRACE_PRINT("closing port %d IP %pI4n", ntohs(addr.sin_port), &addr.sin_addr);
	/* Teardown */
	//kthread_stop(elem->task); //TODO: dont like the elem-> ..., fix teh API when paralel connect is on

	/* TX partner stopped - free qp*/
	if (qp)
		kmem_cache_free(qp_slab, qp);

	/* free both sockets*/
	rc = line = 0;
/*
connect_fail:
	sock_release(tx);
*/
create_fail:
	sock_release(rx);
	TRACE_PRINT("out [%d - %d]", rc, ++line);
	DUMP_TRACE
	return rc;
}

struct tennat_ctx {
	struct socket *sock;
};

static struct tennat_ctx tennats[1] = {0};
static inline void register_server_sock(uint32_t tid, struct socket *sock)
{
	tennats[tid].sock = sock;
}

static inline void stop_sockets(void)
{
	TRACE_PRINT("%s\n", __FUNCTION__);
	kernel_sock_shutdown(tennats[0].sock, SHUT_RDWR);
	sock_release(tennats[0].sock);

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
	register_server_sock(0, sock);

	do {
		struct socket *nsock;
		//struct pool_elem *elem; TODO: nothing to do with elem right now - will need on para-connect
		//sock->sk->sk_rcvtimeo = 1 * HZ;

		rc = kernel_accept(sock, &nsock, 0);
		if (unlikely(rc))
			goto out;

		TRACE_PRINT("starting new connection...\n");
		kthread_pool_run(&cbn_pool, start_new_connection, nsock);

	} while (!kthread_should_stop());

accept_failed:
listen_failed:
bind_failed:
	TRACE_PRINT("Exiting %d\n", rc);
	sock_release(sock);
out:
	DUMP_TRACE
	return rc;
}

int __init cbn_datapath_init(void)
{
	server_task = kthread_run(split_server, NULL, "split_server");
	INIT_LIST_HEAD(&task_list);
	qp_slab = kmem_cache_create("cbn_qp_mdata",
					sizeof(struct cbn_qp), 0, 0, NULL);

	syn_slab = kmem_cache_create("cbn_syn_mdata",
					sizeof(struct addresses), 0, 0, NULL);

	cbn_kthread_pool_init(&cbn_pool);
	nf_register_hooks(cbn_nf_hooks, ARRAY_SIZE(cbn_nf_hooks));
	return 0;
}

void __exit cbn_datapath_clean(void)
{
	pr_err("stopping server_task\n");
	stop_sockets();
	kthread_stop(server_task);
	pr_err("server_task stopped stopping stop_proxies\n");
	stop_proxies();
	pr_err("proxies stopped\n");
	kmem_cache_destroy(qp_slab);
	nf_unregister_hooks(cbn_nf_hooks,  ARRAY_SIZE(cbn_nf_hooks));
}

module_init(cbn_datapath_init);
module_exit(cbn_datapath_clean);

