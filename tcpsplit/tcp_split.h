#ifndef __CBN_DATAPATH_H__
#define __CBN_DATAPATH_H__

#include "rb_data_tree.h"

#define PRECONN_SERVER_PORT	5565

//Lowest official IANA unassigned port
#define CBP_PROBE_PORT 	4

struct sockets {
	struct socket *rx;
	struct socket *tx;
	int 	dir;
};

struct addresses {
	struct sockaddr_in dest;
	struct sockaddr_in src;
	struct in_addr	sin_addr;
	int mark;
};

struct probe {
	struct iphdr iphdr;
	struct tcphdr tcphdr;
	struct cbn_listner *listner;
};

#define UINT_SHIFT	32
static inline void* uint2void(uint32_t a, uint32_t b)
{
	return (void *)((((uint64_t)a)<<UINT_SHIFT)|b);
}

static inline void void2uint(void *ptr, uint32_t *a, uint32_t *b)
{
	uint64_t concat = (uint64_t)ptr;
	*b = ((concat << UINT_SHIFT) >> UINT_SHIFT);
	*a = (concat >> UINT_SHIFT);
}

int half_duplex(struct sockets *sock, struct cbn_qp *qp);

void add_server_cb(int tid, int port);
void del_server_cb(int tid);
void preconn_write_cb(int *);
inline char* proc_read_string(int *);

struct socket *craete_prec_conn_probe(u32 mark);

int __init cbn_pre_connect_init(void);
int __exit cbn_pre_connect_end(void);

int start_probe_syn(void *arg);
int start_new_connection_syn(void *arg);
inline int wait_qp_ready(struct cbn_qp* qp, uint8_t dir);
inline struct cbn_qp *qp_exists(struct cbn_qp* pqp, uint8_t dir);
#endif /*__CBN_DATAPATH_H__*/
