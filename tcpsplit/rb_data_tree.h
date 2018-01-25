#ifndef __CBN_RB_TREE_H__
#define __CBN_RB_TREE_H__

#include <linux/rbtree.h>
#include <linux/types.h> //atomic_t
#include "cbn_common.h"

#define RB_KEY_LENGTH 12
static char chars[(RB_KEY_LENGTH << 3)] = {0};

struct cbn_qp {
	struct rb_node node;
	union {
		char key[RB_KEY_LENGTH];
		struct {
			__be16		port_s;	/* Port number			*/
			__be16		port_d;	/* Port number			*/
			struct in_addr	addr_s;	/* Internet address		*/
			struct in_addr	addr_d;	/* Internet address		*/
		};
		struct {
			int tid;

		};
	};
	atomic_t ref_cnt;

	struct rb_root *root;
	struct list_head list;
	volatile struct socket *tx;
	volatile struct socket *rx;
};

struct cbn_listner {
	struct rb_node 	node;
	struct rb_root  connections_root;
	int32_t		key; //tid
	uint16_t	port;
	uint16_t	status;
	struct socket	*sock;
};
/*
 * Multiple listner support:
 * 1. listner_root
 * 2. unpaird qp root - the syn(tx) side doesnt know which listner is "his" but the "rx" does - move after qp bind
 * */

static inline void show_key(char *key)
{
	int i;
	for (i = 0; i < RB_KEY_LENGTH; ++i)
		sprintf(&chars[i * 3], "%03x", key[i]);
	pr_err("%s: \"%s\"\n", __FUNCTION__, chars);
}

static inline struct cbn_qp *search_rb_data(struct rb_root *root, char *string)
{
	struct rb_node *node = root->rb_node;

	while (node) {
		struct cbn_qp *this = container_of(node, struct cbn_qp, node);
		int result;

		result = strncmp(string, this->key, RB_KEY_LENGTH);

		if (result < 0)
			node = node->rb_left;
		else if (result > 0)
			node = node->rb_right;
		else
			return this;
	}

	return NULL;
}

static inline struct cbn_qp *add_rb_data(struct rb_root *root, struct cbn_qp *data)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;

	/* Figure out where to put new node */
	while (*new) {
		struct cbn_qp *this = container_of(*new, struct cbn_qp, node);
		int result = strncmp(data->key, this->key, RB_KEY_LENGTH);

		parent = *new;
		if (result < 0)
			new = &((*new)->rb_left);
		else if (result > 0)
			new = &((*new)->rb_right);
		else
			return this;
	}

	/* Add new node and rebalance tree. */
	rb_link_node(&data->node, parent, new);
	rb_insert_color(&data->node, root);

	return NULL;
}

static inline struct cbn_listner *search_rb_listner(struct rb_root *root, int32_t key)
{
	struct rb_node *node = root->rb_node;

	while (node) {
		struct cbn_listner *this = container_of(node, struct cbn_listner, node);

		int32_t result = key - this->key;

		if (result < 0)
			node = node->rb_left;
		else if (result > 0)
			node = node->rb_right;
		else
			return this;
	}

	return NULL;
}

static inline struct cbn_listner *add_rb_listner(struct rb_root *root, struct cbn_listner *data)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;

	while (*new) {
		struct cbn_listner *this = container_of(*new, struct cbn_listner, node);
		int32_t result = data->key - this->key;

		parent = *new;
		if (result < 0)
			new = &((*new)->rb_left);
		else if (result > 0)
			new = &((*new)->rb_right);
		else
			return this; //Return the duplicat
	}

	/* Add new node and rebalance tree. */
	rb_link_node(&data->node, parent, new);
	rb_insert_color(&data->node, root);

	return NULL;
}


#endif /*__CBN_RB_TREE_H__*/
