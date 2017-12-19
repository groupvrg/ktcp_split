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

static int start_new_pre_connection_syn(void *arg)
{
	int rc, T = 1;
	struct addresses *addresses = arg;
	struct cbn_listner *listner;
	struct cbn_qp *qp, *tx_qp;
	struct sockets sockets;
	struct socket *tx;

	INIT_TRACE

	qp = kmem_cache_alloc(qp_slab, GFP_KERNEL);
	qp->addr_d = addresses->dest.sin_addr;
	qp->port_s = addresses->src.sin_port;
	qp->port_d = addresses->dest.sin_port;
	qp->addr_s = addresses->src.sin_addr;

	/**
	 * TODO:
	 *  0. alloc new existing connection
	 *  1. send connection info to other side
	 * */

	qp->tx = tx;
	qp->rx = NULL;

	listner = search_rb_listner(&listner_root, addresses->mark);
	kmem_cache_free(syn_slab, addresses);
	//TODO: add locks to this shit
	TRACE_LINE();
	if ((tx_qp = add_rb_data(&listner->connections_root, qp))) { //this means the other conenction is already up
		tx_qp->tx = tx;
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
	sockets.tx = (struct socket *)qp->rx;
	sockets.rx = (struct socket *)qp->tx;
	TRACE_PRINT("starting half duplex");
	half_duplex(&sockets);
	goto connect_fail;


connect_fail:
	sock_release(tx);
create_fail:
	TRACE_PRINT("OUT: connection to port %s ", __FUNCTION__);
	DUMP_TRACE
	return rc;
}

static int start_new_pending_connection(void *arg)
{
	int rc, T = 1;
	struct addresses *addresses = arg;
	struct cbn_listner *listner;
	struct cbn_qp *qp, *tx_qp;
	struct sockets sockets;
	struct socket *tx;

	INIT_TRACE

	/***
	 * TODO: pending function listening to next hop info from prev connection (leave option to chain preexisting connections)
	 *
	 */
	qp = kmem_cache_alloc(qp_slab, GFP_KERNEL);
	qp->addr_d = addresses->dest.sin_addr;
	qp->port_s = addresses->src.sin_port;
	qp->port_d = addresses->dest.sin_port;
	qp->addr_s = addresses->src.sin_addr;


	TRACE_PRINT("connection to port %d IP %pI4n", ntohs(qp->port_d), &qp->addr_d);
	if ((rc = sock_create_kern(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP, &tx)))
		goto create_fail;

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
	qp->rx = NULL;

	listner = search_rb_listner(&listner_root, addresses->mark);
	kmem_cache_free(syn_slab, addresses);
	//TODO: add locks to this shit
	TRACE_LINE();
	if ((tx_qp = add_rb_data(&listner->connections_root, qp))) { //this means the other conenction is already up
		tx_qp->tx = tx;
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
	sockets.tx = (struct socket *)qp->rx;
	sockets.rx = (struct socket *)qp->tx;
	TRACE_PRINT("starting half duplex");
	half_duplex(&sockets);
	goto connect_fail;


connect_fail:
	sock_release(tx);
create_fail:
	TRACE_PRINT("OUT: connection to port %s ", __FUNCTION__);
	DUMP_TRACE
	return rc;
}


static int prealloc_connection(void *arg)
{
	int rc, optval = 1;
	struct addresses *addresses = arg;
	struct cbn_listner *listner;
	struct cbn_qp *qp, *tx_qp;
	struct sockets sockets;
	struct socket *tx;

	INIT_TRACE

	qp = kmem_cache_alloc(qp_slab, GFP_KERNEL);
	qp->addr_d = addresses->dest.sin_addr;
	//qp->port_s = addresses->src.sin_port;
	qp->port_d = addresses->dest.sin_port;
	//qp->addr_s = addresses->src.sin_addr;


	TRACE_PRINT("connection to port %d IP %pI4n", ntohs(qp->port_d), &qp->addr_d);
	if ((rc = sock_create_kern(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP, &tx)))
		goto create_fail;

	if ((rc = kernel_setsockopt(tx, SOL_SOCKET, SO_MARK, (char *)&addresses->mark, sizeof(u32))) < 0)
		goto connect_fail;

	if ((rc = kernel_setsockopt(tx, SOL_SOCKET, SO_KEEPALIVE, optval, sizeof(int))) < 0)
		goto connect_fail;

	TRACE_PRINT("connection to port %d IP %pI4n", ntohs(qp->port_d), &qp->addr_d);
	addresses->dest.sin_family = AF_INET;
	if ((rc = kernel_connect(tx, (struct sockaddr *)&addresses->dest, sizeof(struct sockaddr), 0)))
		goto connect_fail;

	TRACE_PRINT("connection to port %d IP %pI4n", ntohs(qp->port_d), &qp->addr_d);
	qp->tx = tx;
	qp->rx = NULL;

	/****
	 *
	 * TODO:
	 * Add to list of sockets (add new kmemcache)- maybe in rb_tree for diff connections
	 * TODO:
	 *  run this function in loop called by an allocated thread from proc
	 */

connect_fail:
	sock_release(tx);
create_fail:
	TRACE_PRINT("OUT: connection to port %s ", __FUNCTION__);
	DUMP_TRACE
	return rc;
}

//TODO: pretty up this shit
static int listner_server(void *mark_port)
{
	int rc = 0;
	struct socket *sock = NULL;
	struct sockaddr_in srv_addr;
	struct cbn_listner *server;
	u32 mark, port;

	INIT_TRACE

	void2uint(mark_port, &mark, &port);

	if ((rc = sock_create_kern(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP, &sock)))
		goto error;

	server->sock = sock;
	server->port = port;

	srv_addr.sin_family 		= AF_INET;
	srv_addr.sin_addr.s_addr 	= htonl(INADDR_ANY);
	srv_addr.sin_port 		= htons(port);

	if ((rc = kernel_bind(sock, (struct sockaddr *)&srv_addr, sizeof(srv_addr))))
		goto error;

	if ((rc = kernel_listen(sock, BACKLOG)))
		goto error;

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
		//kthread_pool_run(&cbn_pool, start_new_connection, qp); - start_new_pending_connection

	} while (!kthread_should_stop());
error:
	pr_err("Exiting %d\n", rc);
out:
	if (sock)
		sock_release(sock);
	DUMP_TRACE
	return rc;
}

int __init cbn_pre_connect_init(void)
{
	/*****
	 * TODO:
	 * 0. start server
	 * 1. get pool ctx + proc/debugfs dir
	 * 2. create kmemcache for local
	 * 3. add proc iface
	 */
}
