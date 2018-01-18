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

extern struct kthread_pool cbn_pool;
extern struct kmem_cache *qp_slab;
extern struct kmem_cache *syn_slab;
extern struct rb_root listner_root;

static struct list_head pre_conn_list_server;
static struct list_head pre_conn_list_client;
static int next_hop_ip;

static int prealloc_connection(void *arg);

static inline struct cbn_qp *alloc_prexeisting_conn(void)
{
	struct cbn_qp *elem;
	while (unlikely(list_empty(&pre_conn_list_client))) {
		pr_err("pool is empty refill is to slow\n");
		return NULL;
	}

	elem = list_first_entry(&pre_conn_list_client, struct cbn_qp, list);
	list_del(&elem->list);
	kthread_pool_run(&cbn_pool, prealloc_connection, (void *)next_hop_ip);
	return elem;
}

static inline int forward_conn_info(struct socket *tx, struct addresses *addressess)
{
	int rc;
	struct msghdr msg = { 0 };
	struct kvec kvec[1];

	kvec[0].iov_base = addressess;
	kvec[0].iov_len = sizeof(struct addresses);

	if ((rc = kernel_sendmsg(tx, &msg, kvec, 1, sizeof(struct addresses))) <= 0) {
		pr_err("Failed to forward info to next hop!\n");
	}
	return rc;
}

int start_new_pre_connection_syn(void *arg)
{
	int rc;
	struct addresses *addresses = arg;
	struct cbn_listner *listner;
	struct cbn_qp *qp, *tx_qp;
	struct sockets sockets;

	INIT_TRACE

	qp = alloc_prexeisting_conn();

	qp->addr_d = addresses->dest.sin_addr;
	qp->port_s = addresses->src.sin_port;
	qp->port_d = addresses->dest.sin_port;
	qp->addr_s = addresses->src.sin_addr;

	if ((rc	= forward_conn_info(qp->tx, addresses)) <= 0)
		goto connect_fail;
	/**
	 * TODO:
	 *  0.1 add proc to seup connection addr and create prexisting connections.
	 *  2. add teardown for client and server
	 * */

	listner = search_rb_listner(&listner_root, addresses->mark);
	kmem_cache_free(syn_slab, addresses);
	//TODO: add locks to this shit
	TRACE_LINE();
	if ((tx_qp = add_rb_data(&listner->connections_root, qp))) { //this means the other conenction is already up
		tx_qp->tx = qp->tx;
		kmem_cache_free(qp_slab, qp);
		qp = tx_qp;
		TRACE_PRINT("QP exists");
	} else {
		TRACE_PRINT("QP created...");
		while (!qp->rx) {
			if (kthread_should_stop())
				goto connect_fail;
			schedule();
		}
	}
	TRACE_LINE();
	DUMP_TRACE
	sockets.tx 	= (struct socket *)qp->rx;
	sockets.rx 	= (struct socket *)qp->tx;
	qp->root 	= &listner->connections_root;
	TRACE_PRINT("starting half duplex");
	half_duplex(&sockets, qp);

connect_fail:
	sock_release(qp->tx);
	TRACE_PRINT("OUT: connection to port %s ", __FUNCTION__);
	DUMP_TRACE
	return rc;
}

#define PRECONN_SERVER_PORT	51000

static inline void fill_preconn_address(int ip, struct addresses *addresses)
{
	struct in_addr addr;

	addr.s_addr = htonl(ip);
	addresses->dest.sin_family = AF_INET;
	addresses->dest.sin_addr = addr;
	addresses->dest.sin_port = htons(PRECONN_SERVER_PORT);
}

static int prealloc_connection(void *arg)
{
	int rc, optval = 1;
	int ip = (int) arg;
	struct addresses addresses_s = {0};
	struct addresses *addresses = &addresses_s;
	struct cbn_qp *qp;
	struct socket *tx;

	INIT_TRACE
	fill_preconn_address(ip, addresses);
	qp = kmem_cache_alloc(qp_slab, GFP_KERNEL);
	qp->addr_d = addresses->dest.sin_addr;
	qp->port_d = addresses->dest.sin_port;

	if ((rc = sock_create_kern(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP, &tx)))
		goto out;

	if ((rc = kernel_setsockopt(tx, SOL_SOCKET, SO_MARK, (char *)&addresses->mark, sizeof(u32))) < 0)
		goto connect_fail;

	if ((rc = kernel_setsockopt(tx, SOL_SOCKET, SO_KEEPALIVE, (char *)&optval, sizeof(int))) < 0)
		goto connect_fail;

	if ((rc = kernel_connect(tx, (struct sockaddr *)&addresses->dest, sizeof(struct sockaddr), 0)))
		goto connect_fail;

	TRACE_PRINT("connection to port %d IP %pI4n", ntohs(qp->port_d), &qp->addr_d);
	qp->tx = tx;
	qp->rx = NULL;

	//TODO: protect with lock
	list_add(&qp->list, &pre_conn_list_client);
	goto out;

connect_fail:
	sock_release(tx);
out:
	TRACE_PRINT("pre-connection out %s ", __FUNCTION__);
	DUMP_TRACE
	return rc;
}

static inline int preconn_wait_for_next_hop(struct cbn_qp *qp,
					struct addresses *addressess)
{

	struct msghdr msg = { 0 };
	struct kvec kvec;
	int rc;

	kvec.iov_base = addressess;
	kvec.iov_len = sizeof(struct addresses);

	if ((rc = kernel_recvmsg((struct socket *)qp->rx, &msg, &kvec, 1, sizeof(struct addresses), MSG_WAITALL)) <= 0)
		goto err;
err:
	return rc;
}

static int start_half_duplex(void *arg)
{
	void **args = arg;
	TRACE_PRINT("starting half duplex");
	half_duplex(args[0], args[1]);
	return 0;
}

static int start_new_pending_connection(void *arg)
{
	int rc;
	struct cbn_qp *qp = arg;
	struct addresses addresses_s;
	struct addresses *addresses;
	struct sockets sockets, sockets_tx;
	struct socket *tx;
	void *ptr_pair[2];

	INIT_TRACE

	if ((rc = sock_create_kern(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP, &tx)))
		goto create_fail;

	addresses = &addresses_s;
	if ((rc = preconn_wait_for_next_hop(qp, addresses)) <= 0) {
		pr_err("waiting for next hop failed %d\n", rc);
		goto create_fail;
	}

	if ((rc = kernel_setsockopt(tx, SOL_SOCKET, SO_MARK, (char *)&addresses->mark, sizeof(u32))) < 0)
		goto connect_fail;

/*
	if (ip_transparent) {
		if ((rc = kernel_setsockopt(tx, SOL_IP, IP_TRANSPARENT, (char *)&T, sizeof(int))))
			goto connect_fail;

		if ((rc = kernel_bind(tx, (struct sockaddr *)&addresses->src, sizeof(struct sockaddr))))
			goto connect_fail;
	}
*/
	addresses->dest.sin_family = AF_INET;
	if ((rc = kernel_connect(tx, (struct sockaddr *)&addresses->dest, sizeof(struct sockaddr), 0)))
		goto connect_fail;

	qp->tx = tx;

	TRACE_LINE();
	DUMP_TRACE

	sockets.tx = (struct socket *)qp->rx;
	sockets.rx = (struct socket *)qp->tx;
	sockets.dir = 0;

	TRACE_PRINT("starting half duplex");
	half_duplex(&sockets, qp);

	sockets_tx.rx = (struct socket *)qp->rx;
	sockets_tx.tx = (struct socket *)qp->tx;
	sockets_tx.dir = 1;

	TRACE_PRINT("starting half duplex");
	half_duplex(&sockets, qp);

	ptr_pair[0] = &sockets_tx;
	ptr_pair[1] = qp;
	atomic_inc(&qp->ref_cnt);
	kthread_pool_run(&cbn_pool, start_half_duplex, ptr_pair);
connect_fail:
	sock_release(tx);
	sock_release((struct socket *)qp->rx);
create_fail:
	TRACE_PRINT("OUT: connection to port %s ", __FUNCTION__);
	DUMP_TRACE
	return rc;
}

static struct cbn_listner *pre_conn_listner;

static void preconn_resgister_server(struct cbn_listner *server)
{
	pre_conn_listner 		= server;
}

#define BACKLOG 16

static int prec_conn_listner_server(void *arg)
{
	int rc = 0;
	struct socket *sock = NULL;
	struct sockaddr_in srv_addr;
	struct cbn_listner server_s;
	struct cbn_listner *server;
	u32 port;

	INIT_TRACE

	TRACE_PRINT("Pre conn server running");
	port = (u32)arg;
	if ((rc = sock_create_kern(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP, &sock)))
		goto error;

	server = &server_s;
	server->sock = sock;
	server->port = port;

	srv_addr.sin_family 		= AF_INET;
	srv_addr.sin_addr.s_addr 	= htonl(INADDR_ANY);
	srv_addr.sin_port 		= htons(port);

	if ((rc = kernel_bind(sock, (struct sockaddr *)&srv_addr, sizeof(srv_addr))))
		goto error;

	if ((rc = kernel_listen(sock, BACKLOG)))
		goto error;

	TRACE_PRINT("Listening to new pre connections");
	preconn_resgister_server(server);
	do {
		struct socket *nsock;
		struct cbn_qp *qp;

		rc = kernel_accept(sock, &nsock, 0);
		if (unlikely(rc))
			goto out;

		TRACE_PRINT("new pre connection...");
		qp = kmem_cache_alloc(qp_slab, GFP_KERNEL);
		qp->rx 		= nsock;
		qp->tid 	= 0;
		atomic_set(&qp->ref_cnt, 1);
		qp->root 	= NULL;
		INIT_LIST_HEAD(&qp->list);

		list_add(&qp->list, &pre_conn_list_server);
		kthread_pool_run(&cbn_pool, start_new_pending_connection, qp);

	} while (!kthread_should_stop());
	error:
	pr_err("Exiting %d\n", rc);
	out:
	if (sock)
		sock_release(sock);
	DUMP_TRACE
	return rc;
}

static inline int build_ip(int *array)
{
	int i, ip = 0;
	for (i = 0; i < 4; i++)
		if (array[i] > 255)
			return 0;
		else
			ip = (ip << 4)|array[i];
	return ip;
}

void preconn_write_cb(int *array)
{
	int ip;

	ip = build_ip(array);
	if (next_hop_ip && next_hop_ip != ip) {
		pr_err("Already have an existing next_hop_ip!!\n");
	}


	if (ip) {
		pr_info("connecting to %d.%d.%d.%d (%x)\n", array[0], array[1], array[2], array[3], ip);
		next_hop_ip = ip;
		kthread_pool_run(&cbn_pool, prealloc_connection, (void *)ip);
		kthread_pool_run(&cbn_pool, prealloc_connection, (void *)ip);
		kthread_pool_run(&cbn_pool, prealloc_connection, (void *)ip);
		kthread_pool_run(&cbn_pool, prealloc_connection, (void *)ip);
	} else {
		pr_err("ip is invalid %d.%d.%d.%d\n",array[0], array[1], array[2], array[3]);
	}
}

int __init cbn_pre_connect_init(void)
{
	int port = PRECONN_SERVER_PORT;
	INIT_LIST_HEAD(&pre_conn_list_client);
	INIT_LIST_HEAD(&pre_conn_list_server);
	kthread_pool_run(&cbn_pool, prec_conn_listner_server, &port);

	return 0;
}

int __exit cbn_pre_connect_end(void)
{
	struct list_head *itr, *tmp;

	list_for_each_safe(itr, tmp, &pre_conn_list_client) {
		struct cbn_qp *elem = container_of(itr, struct cbn_qp, list);
		list_del(itr);
		sock_release(elem->tx);
		kmem_cache_free(qp_slab, elem);
	}

	list_for_each_safe(itr, tmp, &pre_conn_list_server) {
		struct cbn_qp *elem = container_of(itr, struct cbn_qp, list);
		list_del(itr);
		sock_release(elem->rx);
		kmem_cache_free(qp_slab, elem);
	}

	return 0;
}
