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

struct kthread_pool cbn_pool = {.pool_size = DEF_CBN_POOL_SIZE};

struct rb_root listner_root = RB_ROOT;

struct kmem_cache *qp_slab;
struct kmem_cache *syn_slab;

static struct kmem_cache *listner_slab;

uint32_t ip_transparent = 0;
static int start_new_connection_syn(void *arg);
extern long next_hop_ip;
extern int start_new_pre_connection_syn(void *arg);

static unsigned int put_qp(struct cbn_qp *qp)
{
	int rc;
	if (! (rc = atomic_dec_return(&qp->ref_cnt))) {
		// TODO: protect with lock on MC
		// reusable connections may not have a root
		if (qp->root)
			rb_erase(&qp->node, qp->root);
		else
			list_del(&qp->list);
		kmem_cache_free(qp_slab, qp);
	}
	return rc;
}

/*
static unsigned int cbn_trace_hook(void *priv,
					struct sk_buff *skb,
					const struct nf_hook_state *state)
{
	trace_iph(skb, priv);
	return NF_ACCEPT;
}
*/

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
		TRACE_PRINT("%s scheduling start_new_connection_syn [%lx]", __FUNCTION__, next_hop_ip);
		if (next_hop_ip)
			kthread_pool_run(&cbn_pool, start_new_pre_connection_syn, addresses); //elem?
		else
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
	/*
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
		{
		.hook		= cbn_ingress_hook,
		.hooknum	= NF_INET_LOCAL_IN,
		.pf		= PF_INET,
		.priority	= NF_IP_PRI_FIRST,
		.priv		= "LIN"
		},

*/
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
			sock = (struct socket *)pos->tx;
			kernel_sock_shutdown(sock, SHUT_RDWR);
		}
		if (pos->rx) {
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

#define VEC_SZ 16
int half_duplex(struct sockets *sock, struct cbn_qp *qp)
{
	struct kvec kvec[VEC_SZ];
	int id = 0, i ,dir = sock->dir;
	int rc = -ENOMEM;
	uint64_t bytes = 0;

	INIT_TRACE

	for (i = 0; i < VEC_SZ; i++) {
		kvec[i].iov_len = PAGE_SIZE;
		if (! (kvec[i].iov_base = page_address(alloc_page(GFP_KERNEL))))
			goto err;
	}

	do {
		struct msghdr msg = { 0 };
		if ((rc = kernel_recvmsg(sock->rx, &msg, kvec, VEC_SZ, (PAGE_SIZE * VEC_SZ), 0)) <= 0) {
			if (put_qp(qp))
				kernel_sock_shutdown(sock->tx, SHUT_RDWR);
			goto err;
		}
		bytes += rc;
		id ^= 1;
		//use kern_sendpage if flags needed.
		if ((rc = kernel_sendmsg(sock->tx, &msg, kvec, VEC_SZ, rc)) <= 0) {
			if (put_qp(qp))
				kernel_sock_shutdown(sock->rx, SHUT_RDWR);
			goto err;
		}
		id ^= 1;
	} while (!kthread_should_stop());

	goto out;
err:
	pr_err("%s [%s] stopping on error (%d) at %s with %lld bytes\n", __FUNCTION__,
		dir  ? "TX" : "RX", rc, id ? "Send" : "Rcv", bytes);
out:
	TRACE_PRINT("%s going out (%d)", __FUNCTION__, rc);
	for (i = 0; i < VEC_SZ; i++)
		free_page((unsigned long)(kvec[i].iov_base));
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
	atomic_set(&qp->ref_cnt, 1);

	qp->tx = ERR_PTR(-EINVAL);

	TRACE_PRINT("connection to port %d IP %pI4n", ntohs(qp->port_d), &qp->addr_d);
	if ((rc = sock_create_kern(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP, &tx))) {
		pr_err("%s error (%d)\n", __FUNCTION__, rc);
		goto connect_fail;
	}

	if ((rc = kernel_setsockopt(tx, SOL_SOCKET, SO_MARK, (char *)&addresses->mark, sizeof(u32))) < 0) {
		pr_err("%s error (%d)\n", __FUNCTION__, rc);
		goto connect_fail;
	}

	if (ip_transparent) {
		if ((rc = kernel_setsockopt(tx, SOL_IP, IP_TRANSPARENT, (char *)&T, sizeof(int))))
			goto connect_fail;

		if ((rc = kernel_bind(tx, (struct sockaddr *)&addresses->src, sizeof(struct sockaddr))))
			goto connect_fail;
	}

	addresses->dest.sin_family = AF_INET;
	if ((rc = kernel_connect(tx, (struct sockaddr *)&addresses->dest, sizeof(struct sockaddr), 0))) {
		pr_err("%s error (%d)\n", __FUNCTION__, rc);
		goto connect_fail;
	}

	qp->tx = tx;
connect_fail:
	qp->rx = NULL;

	listner = search_rb_listner(&listner_root, addresses->mark);
	qp->root = &listner->connections_root;

	kmem_cache_free(syn_slab, addresses);
	//TODO: add locks to this shit
	TRACE_PRINT("%s qp %p listner %p mark %d", __FUNCTION__, qp, listner, addresses->mark);
	if ((tx_qp = add_rb_data(qp->root, qp))) { //this means the other conenction is already up
		tx_qp->tx = tx;
		kmem_cache_free(qp_slab, qp);
		qp = tx_qp;
		atomic_inc(&qp->ref_cnt);
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
	DUMP_TRACE
	sockets.tx = (struct socket *)qp->rx;
	sockets.rx = (struct socket *)qp->tx;
	sockets.dir = 1;
	TRACE_PRINT("starting half duplex %d", atomic_read(&qp->ref_cnt));
	if (IS_ERR_OR_NULL((struct socket *)qp->rx) || IS_ERR_OR_NULL((struct socket *)qp->tx))
		goto out;
	rc = half_duplex(&sockets, qp);

out:
	if (tx)
		sock_release(tx);

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
	if ((rc = kernel_getsockopt(rx, SOL_IP, SO_ORIGINAL_DST, (char *)&addr, &size))) {
		pr_err("%s error (%d)\n", __FUNCTION__, rc);
		goto create_fail;
	}

	if ((rc = kernel_setsockopt(rx, SOL_SOCKET, SO_MARK, (char *)&mark, sizeof(u32))) < 0) {
		pr_err("%s error (%d)\n", __FUNCTION__, rc);
		goto create_fail;
	}

	line = __LINE__;
	if ((rc = kernel_getpeername(rx, (struct sockaddr *)&cli_addr, &size))) {
		pr_err("%s error (%d)\n", __FUNCTION__, rc);
		goto create_fail;
	}

/*
	line = __LINE__;
	if ((rc = kernel_getsockname(tx, (struct sockaddr *)&addr, &size)))
		goto connect_fail;
		*/
	TRACE_PRINT("connected local port %d IP %pI4n (%d)", ntohs(addr.sin_port), &addr.sin_addr, addr.sin_family);

	qp->addr_d = addr.sin_addr;
	qp->port_s = cli_addr.sin_port;
	qp->port_d = addr.sin_port;
	qp->addr_s = cli_addr.sin_addr;
	/*rp->root/qp->mark no longer valid, qp is a union*/
	qp->tx = NULL;

	if ((tx_qp = add_rb_data(root, qp))) { //this means the other conenction is already up
		TRACE_PRINT("QP exists");
		tx_qp->rx = rx;
		kmem_cache_free(qp_slab, qp);
		qp = tx_qp;
		atomic_inc(&qp->ref_cnt);
	} else {
		TRACE_PRINT("QP created...");
		while (!qp->tx) {
			if (kthread_should_stop())
				goto create_fail;
			schedule();
		}
	}

	TRACE_PRINT("starting half duplex %d", atomic_read(&qp->ref_cnt));
	DUMP_TRACE
	sockets.tx = (struct socket *)qp->tx;
	sockets.rx = (struct socket *)qp->rx;
	sockets.dir = 0;
	if (IS_ERR_OR_NULL((struct socket *)(qp->rx)) || IS_ERR_OR_NULL((struct socket *)qp->tx))
		goto out;
	half_duplex(&sockets, qp);
out:
	TRACE_PRINT("closing port %d IP %pI4n", ntohs(addr.sin_port), &addr.sin_addr);
	/* Teardown */
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
	struct cbn_listner *server = NULL;
	u32 mark, port;

	INIT_TRACE

	void2uint(mark_port, &mark, &port);
	if (search_rb_listner(&listner_root, mark)) {
		rc = -EEXIST;
		pr_err("server exists: %d @ %d", mark, port);
		goto error;
	}

	server = register_server_sock(mark, sock);

	server->status = 1;
	if ((rc = sock_create_kern(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP, &sock)))
		goto error;

	server->sock = sock;
	server->port = port;
	server->status = 2;

	if ((rc = kernel_setsockopt(sock, SOL_SOCKET, SO_MARK, (char *)&mark, sizeof(u32))) < 0)
		goto error;

	server->status = 3;
	srv_addr.sin_family 		= AF_INET;
	srv_addr.sin_addr.s_addr 	= htonl(INADDR_ANY);
	srv_addr.sin_port 		= htons(port);

	if ((rc = kernel_bind(sock, (struct sockaddr *)&srv_addr, sizeof(srv_addr))))
		goto error;

	server->status = 4;
	TRACE_PRINT("tenant %d: new listner on port %d", mark, port);
	if ((rc = kernel_listen(sock, BACKLOG)))
		goto error;

	server->status = 5;

	do {
		struct socket *nsock;
		struct cbn_qp *qp;

		rc = kernel_accept(sock, &nsock, 0);
		if (unlikely(rc))
			goto out;

		qp = kmem_cache_alloc(qp_slab, GFP_KERNEL);
		qp->rx 		= nsock;
		qp->tid 	= mark;
		qp->root 	= &server->connections_root;
		atomic_set(&qp->ref_cnt, 1);
		TRACE_PRINT("%s scheduling start_new_connection [%d]", __FUNCTION__, mark);
		kthread_pool_run(&cbn_pool, start_new_connection, qp);

	} while (!kthread_should_stop());
	server->status = 6;
error:
	TRACE_PRINT("Exiting %d <%d>\n", rc, (server) ? server->status : -1);
out:
	if (sock)
		sock_release(sock);
	DUMP_TRACE
	return rc;
}

void proc_write_cb(int tid, int port)
{
	pr_info("%s scheduling split server", __FUNCTION__);
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
	cbn_pre_connect_init();
	cbn_proc_init();
	return 0;
}

void __exit cbn_datapath_clean(void)
{
	TRACE_PRINT("Removing proc");
	cbn_proc_clean();
	TRACE_PRINT("Removing pre-connections");
	cbn_pre_connect_end();
	nf_unregister_hooks(cbn_nf_hooks,  ARRAY_SIZE(cbn_nf_hooks));
	stop_sockets();
	TRACE_PRINT("sockets stopped");
	cbn_kthread_pool_clean(&cbn_pool);
	TRACE_PRINT("proxies stopped");
	kmem_cache_destroy(qp_slab);
	kmem_cache_destroy(syn_slab);
	kmem_cache_destroy(listner_slab);
}

module_init(cbn_datapath_init);
module_exit(cbn_datapath_clean);

