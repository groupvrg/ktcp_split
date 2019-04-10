#include "tcp_split.h"
#include "rb_data_tree.h"

extern struct kmem_cache *qp_slab;

inline void dump_qp(struct cbn_qp *qp, const char *str)
{
	TRACE_QP("<%s> %s :QP %p: "TCP4" => "TCP4, __FUNCTION__, str, qp,
			TCP4N(&qp->addr_s, ntohs(qp->port_s)),
			TCP4N(&qp->addr_d, ntohs(qp->port_d)));
}

inline void get_qp(struct cbn_qp *qp)
{
	int rc;
	rc = atomic_inc_return(&qp->ref_cnt);
	TRACE_DEBUG("%s : (%p->[%p][%p]) %d", __FUNCTION__, qp, qp->tx, qp->rx, rc);
	switch  (rc) {
	case 2:
		dump_qp(qp, "remove from tree");
		if (qp->listner) {
			struct cbn_root_qp *qp_root = this_cpu_ptr(qp->listner->connections_root);

			de_tree_qp(&qp->node, &qp_root->root, &qp->listner->rb_lock);
		}
		/* else is legitamate in start_new_pending_connection
		 */
		/*Intentional falltrough */
	case 1:
		spin_lock_init(&qp->lock);
		break;
	default:
		TRACE_ERROR("Impossible QP refcount %d", rc);
		dump_qp(qp, "IMPOSSIBLE VALUE");
		break;
	}
}

inline unsigned int put_qp(struct cbn_qp *qp)
{
	int rc;
	unsigned long flags;

	/**
	 * TODO: This whole section must to be atomic, due to enumerable TOCTOU(Race condition) issues...
	 *	spin_lock is enough irq/bh contexts dont work with QPs.
	 */
	spin_lock_irqsave(&qp->lock, flags);
	if (! (rc = atomic_dec_return(&qp->ref_cnt))) {
		//TODO: Consider adding a tree for active QPs + States.
		//TODO: Add waitqueue here...
		if (!IS_ERR_OR_NULL(qp->tx))
			sock_release(qp->tx);
		if (!IS_ERR_OR_NULL(qp->rx))
			sock_release(qp->rx);
		kmem_cache_free(qp_slab, qp);
	} else {
		if (!IS_ERR_OR_NULL(qp->tx))
			kernel_sock_shutdown(qp->tx, SHUT_RDWR);
		if (!IS_ERR_OR_NULL(qp->rx))
			kernel_sock_shutdown(qp->rx, SHUT_RDWR);
	}
	spin_unlock_irqrestore(&qp->lock, flags);
	TRACE_DEBUG("%s : (%p->[%p][%p]) %d", __FUNCTION__, qp, qp->tx, qp->rx, rc);
	return rc;
}

inline void* uint2void(uint32_t a, uint32_t b)
{
	return (void *)((((uint64_t)a)<<UINT_SHIFT)|b);
}

inline void void2uint(void *ptr, uint32_t *a, uint32_t *b)
{
	uint64_t concat = (uint64_t)ptr;
	*b = ((concat << UINT_SHIFT) >> UINT_SHIFT);
	*a = (concat >> UINT_SHIFT);
}

inline unsigned int qp2cpu(struct cbn_qp *qp)
{
	unsigned int i = 0;
	unsigned int core = 0;

	char str[32];

	for (;i < RB_KEY_LENGTH; i++)
		core ^= qp->key[i];
	i = num_online_cpus();
	i = (core/i) * i;

	snprintf(str, 32, "core = %u, i = %u => %u", core, i, core - i);
	dump_qp(qp, str);
	return core - i;
}

inline unsigned int addresses2cpu(struct addresses *addr)
{
	struct cbn_qp qp;
	qp.addr_d = addr->dest.sin_addr;
	qp.port_s = addr->src.sin_port;
	qp.port_d = addr->dest.sin_port;
	qp.addr_s = addr->src.sin_addr;
	return qp2cpu(&qp);
}
