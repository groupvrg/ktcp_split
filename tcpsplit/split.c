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
#include "proc.h"
#include "rb_data_tree.h"
#include "cbn_common.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Markuze Alex");
MODULE_DESCRIPTION("CBN TCP Split Module");

#define BACKLOG     64

static struct kthread_pool cbn_pool = {.pool_size = DEF_CBN_POOL_SIZE};

static struct rb_root listner_root = RB_ROOT;

static struct kmem_cache *qp_slab;
static struct kmem_cache *listner_slab;
static struct kmem_cache *syn_slab;

uint32_t ip_transparent = 0;
static int start_new_connection_syn(void *arg);

static unsigned int cbn_trace_hook(void *priv,
					struct sk_buff *skb,
					const struct nf_hook_state *state)
{
	trace_iph(skb, priv);
	return NF_ACCEPT;
}

static unsigned int cbn_ingress_hook(void *priv,
					struct sk_buff *skb,
					const struct nf_hook_state *state)
{
	if (!skb->mark)
		goto out;
	if (trace_iph(skb, priv)) {
		struct iphdr *iphdr = ip_hdr(skb);
		struct tcphdr *tcphdr = (struct tcphdr *)skb_transport_header(skb);
		struct addresses *addresses;

		if (strcmp(priv, "RX"))
			goto out;
//		if (skb->mark != 10)
//			goto out;

		pr_info("schedule connection %d\n", skb->mark);

		addresses = kmem_cache_alloc(syn_slab, GFP_ATOMIC);
		if (unlikely(!addresses)) {
			pr_err("Faield to alloc mem %s\n", __FUNCTION__);
			goto out;
		}
		addresses->dest.sin_addr.s_addr	= iphdr->daddr;
		addresses->src.sin_addr.s_addr	= iphdr->saddr;
		addresses->dest.sin_port	= tcphdr->dest;
		addresses->src.sin_port		= tcphdr->source;
		addresses->mark			= skb->mark;
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
		{
		.hook		= cbn_ingress_hook,
		.hooknum	= NF_INET_LOCAL_OUT,
		.pf		= PF_INET,
		.priority	= NF_IP_PRI_FIRST,
		.priv		= "NF_INET_LOCAL_OUT"
		},

		{
		.hook		= cbn_ingress_hook,
		.hooknum	= NF_INET_FORWARD,
		.pf		= PF_INET,
		.priority	= NF_IP_PRI_FIRST,
		.priv		= "NF_INET_FORWARD"
		},
	/*
		{
		.hook		= cbn_trace_hook,
		.hooknum	= NF_INET_LOCAL_IN,
		.pf		= PF_INET,
		.priority	= (NF_IP_PRI_SECURITY -1),
		.priv		= "SEC-1"
		},
		{
		.hook		= cbn_trace_hook,
		.hooknum	= NF_INET_LOCAL_IN,
		.pf		= PF_INET,
		.priority	= (NF_IP_PRI_SECURITY +1),
		.priv		= "SEC+1"
		},
*/
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

static inline void stop_tennat_proxies(struct rb_root *root)
{

	struct cbn_qp *pos, *tmp;
	struct socket *sock;

	rbtree_postorder_for_each_entry_safe(pos, tmp, root, node) {
		if (pos->tx) {
			pr_err("releasing %p\n", pos->tx);
			sock = (struct socket *)pos->tx;
			kernel_sock_shutdown(sock, SHUT_RDWR);
		}
		if (pos->rx) {
			pr_err("releasing %p\n", pos->rx);
			sock = (struct socket *)pos->rx;
			kernel_sock_shutdown(sock, SHUT_RDWR);
		}
	}
}

static inline void stop_sockets(void)
{
	struct cbn_listner *pos, *tmp;

	rbtree_postorder_for_each_entry_safe(pos, tmp, &listner_root, node) {
		//sock_release(pos->sock); //TODO: again maybe just shut down?
		kernel_sock_shutdown(pos->sock, SHUT_RDWR);
		stop_tennat_proxies(&pos->connections_root);
	}
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
		if ((rc = kernel_recvmsg(qp->rx, &msg, &kvec, 1, PAGE_SIZE, 0)) <= 0) {
			kernel_sock_shutdown(qp->tx, SHUT_RDWR);
			goto err;
		}
		//use kern_sendpage if flags needed.
		if ((rc = kernel_sendmsg(qp->tx, &msg, &kvec, 1, rc)) <= 0) {
			kernel_sock_shutdown(qp->rx, SHUT_RDWR);
			goto err;
		}
	} while (!kthread_should_stop());

	goto out;
err:
	TRACE_PRINT("%s stopping on error (%d)\n", __FUNCTION__, rc);
out:
	TRACE_PRINT("%s going out (%d)\n", __FUNCTION__, rc);
	free_page((unsigned long)(kvec.iov_base));
	DUMP_TRACE
	return rc;
}

static int start_new_connection_syn(void *arg)
{
	int rc, T = 1;
	struct addresses *addresses = arg;
	struct cbn_listner *listner;
	struct cbn_qp *qp, *tx_qp;
	struct sockets sockets;
	struct socket *tx = NULL;

	INIT_TRACE

	qp = kmem_cache_alloc(qp_slab, GFP_KERNEL);
	qp->addr_d = addresses->dest.sin_addr;
	qp->port_s = addresses->src.sin_port;
	qp->port_d = addresses->dest.sin_port;
	qp->addr_s = addresses->src.sin_addr;

	qp->tx = ERR_PTR(-EINVAL);

	TRACE_PRINT("connection to port %d IP %pI4n", ntohs(qp->port_d), &qp->addr_d);
	if ((rc = sock_create_kern(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP, &tx)))
		goto connect_fail;

	if ((rc = kernel_setsockopt(tx, SOL_SOCKET, SO_MARK, (char *)&addresses->mark, sizeof(u32))) < 0)
		goto connect_fail;

	if (ip_transparent) {
		if ((rc = kernel_setsockopt(tx, SOL_IP, IP_TRANSPARENT, (char *)&T, sizeof(int))))
			goto connect_fail;

		if ((rc = kernel_bind(tx, (struct sockaddr *)&addresses->src, sizeof(struct sockaddr))))
			goto connect_fail;
	}
	TRACE_PRINT("connection to port %d IP %pI4n", ntohs(qp->port_d), &qp->addr_d);
	addresses->dest.sin_family = AF_INET;
	if ((rc = kernel_connect(tx, (struct sockaddr *)&addresses->dest, sizeof(struct sockaddr), 0)))
		goto connect_fail;

	TRACE_PRINT("connection to port %d IP %pI4n", ntohs(qp->port_d), &qp->addr_d);
	qp->tx = tx;
connect_fail:
	qp->rx = NULL;

	listner = search_rb_listner(&listner_root, addresses->mark);
	kmem_cache_free(syn_slab, addresses);
	//TODO: add locks to this shit
	TRACE_PRINT("%s qp %p listner %p mark %d", __FUNCTION__, qp, listner, addresses->mark);
	if ((tx_qp = add_rb_data(&listner->connections_root, qp))) { //this means the other conenction is already up
		tx_qp->tx = tx;
		kmem_cache_free(qp_slab, qp);
		qp = tx_qp;
		TRACE_PRINT("QP exists");
	} else {
		//TODO: Must ADD T/O. (accept wont signal with ERR on qp)
		TRACE_PRINT("QP created...");
		while (!qp->rx) {
			if (kthread_should_stop())
				goto out;
			schedule();
		}
	}
	TRACE_LINE();
	DUMP_TRACE
	sockets.tx = (struct socket *)qp->rx;
	sockets.rx = (struct socket *)qp->tx;
	TRACE_PRINT("starting half duplex");
	if (IS_ERR_OR_NULL(qp->rx) || IS_ERR_OR_NULL(qp->tx))
		goto out;
	rc = half_duplex(&sockets);

out:
	if (tx)
		sock_release(tx);
create_fail:
	TRACE_PRINT("OUT: %s <%d>", __FUNCTION__, rc);
	DUMP_TRACE
	return rc;
}

static int start_new_connection(void *arg)
{
	int rc, size, line, mark;
	struct socket *rx;
	struct sockaddr_in cli_addr;
	struct sockaddr_in addr;
	struct cbn_qp *qp, *tx_qp;
	struct rb_root *root;
	struct sockets sockets;

	INIT_TRACE

	qp = arg;
	rx 	= (struct socket *)qp->rx;
	mark 	= qp->tid;
	root 	= qp->root;

	size = sizeof(addr);
	line = __LINE__;
	if ((rc = kernel_getsockopt(rx, SOL_IP, SO_ORIGINAL_DST, (char *)&addr, &size)))
		goto create_fail;

	if ((rc = kernel_setsockopt(rx, SOL_SOCKET, SO_MARK, (char *)&mark, sizeof(u32))) < 0)
		goto create_fail;

	line = __LINE__;
	if ((rc = kernel_getpeername(rx, (struct sockaddr *)&cli_addr, &size)))
		goto create_fail;

/*
	line = __LINE__;
	if ((rc = kernel_getsockname(tx, (struct sockaddr *)&addr, &size)))
		goto connect_fail;
	TRACE_PRINT("connected local port %d IP %pI4n (%d)", ntohs(addr.sin_port), &addr.sin_addr, addr.sin_family);

*/
	qp->addr_d = addr.sin_addr;
	qp->port_s = cli_addr.sin_port;
	qp->port_d = addr.sin_port;
	qp->addr_s = cli_addr.sin_addr;
	/*rp->root/qp->mark no longer valid, qp is a union*/
	qp->tx = NULL;

	TRACE_LINE();
	if ((tx_qp = add_rb_data(root, qp))) { //this means the other conenction is already up
		TRACE_LINE();
		tx_qp->rx = rx;
		kmem_cache_free(qp_slab, qp);
		qp = tx_qp;
	} else {
		TRACE_LINE();
		while (!qp->tx) {
			if (kthread_should_stop())
				goto create_fail;
			schedule();
		}
	}

	TRACE_LINE();
	DUMP_TRACE
	sockets.tx = (struct socket *)qp->tx;
	sockets.rx = (struct socket *)qp->rx;
	if (IS_ERR_OR_NULL(qp->rx) || IS_ERR_OR_NULL(qp->tx))
		goto out;
	half_duplex(&sockets);
out:
	TRACE_PRINT("closing port %d IP %pI4n", ntohs(addr.sin_port), &addr.sin_addr);
	/* Teardown */

	/* TX partner stopped - free qp*/
	if (qp)
		kmem_cache_free(qp_slab, qp);

	/* free both sockets*/
	rc = line = 0;

create_fail:
	sock_release(rx);
	TRACE_PRINT("out [%d - %d]", rc, ++line);
	DUMP_TRACE
	return rc;
}

static inline struct cbn_listner *register_server_sock(uint32_t tid, struct socket *sock)
{
	struct cbn_listner *server = kmem_cache_alloc(listner_slab, GFP_KERNEL);

	server->key			= tid;
	server->sock 			= sock;
	server->connections_root 	= RB_ROOT;

	add_rb_listner(&listner_root, server);
	return server;
}

static int split_server(void *mark_port)
{
	int rc = 0;
	struct socket *sock = NULL;
	struct sockaddr_in srv_addr;
	struct cbn_listner *server;
	u32 mark, port;

	INIT_TRACE

	TRACE_LINE();
	pr_err("starting %s\n", __FUNCTION__);
	void2uint(mark_port, &mark, &port);
	if (search_rb_listner(&listner_root, mark)) {
		rc = -EEXIST;
		goto error;
	}
	TRACE_LINE();

	server = register_server_sock(mark, sock);

	server->status = 1;
	if ((rc = sock_create_kern(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP, &sock)))
		goto error;

	TRACE_LINE();
	server->sock = sock;
	server->port = port;
	server->status = 2;

	TRACE_LINE();
	if ((rc = kernel_setsockopt(sock, SOL_SOCKET, SO_MARK, (char *)&mark, sizeof(u32))) < 0)
		goto error;

	server->status = 3;
	srv_addr.sin_family 		= AF_INET;
	srv_addr.sin_addr.s_addr 	= htonl(INADDR_ANY);
	srv_addr.sin_port 		= htons(port);

	TRACE_LINE();
	if ((rc = kernel_bind(sock, (struct sockaddr *)&srv_addr, sizeof(srv_addr))))
		goto error;

	server->status = 4;
	TRACE_LINE();
	pr_info("%s) tenant %d: new listner on port %d", current->comm, mark, port);
	if ((rc = kernel_listen(sock, BACKLOG)))
		goto error;

	TRACE_LINE();
	server->status = 5;
	//server = register_server_sock(mark, sock);

	do {
		struct socket *nsock;
		struct cbn_qp *qp;

		rc = kernel_accept(sock, &nsock, 0);
		if (unlikely(rc))
			goto out;

		TRACE_PRINT("new connection [%d]...", mark);
		qp = kmem_cache_alloc(qp_slab, GFP_KERNEL);
		qp->rx 		= nsock;
		qp->tid 	= mark;
		qp->root 	= &server->connections_root;
		kthread_pool_run(&cbn_pool, start_new_connection, qp);

	} while (!kthread_should_stop());
	server->status = 6;
error:
	pr_err("Exiting %d\n", rc);
out:
	if (sock)
		sock_release(sock);
	DUMP_TRACE
	return rc;
}

void proc_write_cb(int tid, int port)
{
	kthread_pool_run(&cbn_pool, split_server, uint2void(tid, port));
}

const char *proc_read_string(int *loc)
{
	struct cbn_listner *pos, *tmp;
	int  idx = 0;
	char *buffer = kzalloc(PAGE_SIZE, GFP_KERNEL);

	if (!buffer)
		return NULL;

	rbtree_postorder_for_each_entry_safe(pos, tmp, &listner_root, node) {
		idx += sprintf(&buffer[idx],"tid=%d port=%d status=%d\n",
			       pos->key, pos->port, pos->status);
	}
	*loc = idx;
	return buffer;
}

int __init cbn_datapath_init(void)
{
	qp_slab = kmem_cache_create("cbn_qp_mdata",
					sizeof(struct cbn_qp), 0, 0, NULL);

	listner_slab = kmem_cache_create("cbn_listner",
					 sizeof(struct cbn_listner), 0, 0, NULL);

	syn_slab = kmem_cache_create("cbn_syn_mdata",
					sizeof(struct addresses), 0, 0, NULL);

	cbn_kthread_pool_init(&cbn_pool);
	nf_register_hooks(cbn_nf_hooks, ARRAY_SIZE(cbn_nf_hooks));
	cbn_proc_init();
	return 0;
}

void __exit cbn_datapath_clean(void)
{
	cbn_proc_clean();
	nf_unregister_hooks(cbn_nf_hooks,  ARRAY_SIZE(cbn_nf_hooks));
	stop_sockets();
	pr_info("sockets stopped\n");
	cbn_kthread_pool_clean(&cbn_pool);
	pr_info("proxies stopped\n");
	kmem_cache_destroy(qp_slab);
	kmem_cache_destroy(syn_slab);
	kmem_cache_destroy(listner_slab);
}

module_init(cbn_datapath_init);
module_exit(cbn_datapath_clean);

