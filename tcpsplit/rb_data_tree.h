#ifndef __CBN_RB_TREE_H__
#define __CBN_RB_TREE_H__

#include <linux/rbtree.h>
#include <linux/types.h> //atomic_t
#include "cbn_common.h"
#include "tcp_split.h"

/*
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
*/

static inline struct cbn_qp *add_rb_data(struct rb_root *root, struct cbn_qp *data)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;

	/* Figure out where to put new node */
	local_bh_disable();
	while (*new) {
		struct cbn_qp *this = container_of(*new, struct cbn_qp, node);
		int result = strncmp(data->key, this->key, RB_KEY_LENGTH);

		parent = *new;
		if (result < 0)
			new = &((*new)->rb_left);
		else if (result > 0)
			new = &((*new)->rb_right);
		else  {
			local_bh_enable();
			dump_qp(data, "QP exists.");
			return this;
		}
	}

	/* Add new node and rebalance tree. */
	rb_link_node(&data->node, parent, new);
	rb_insert_color(&data->node, root);
	local_bh_enable();
	dump_qp(data, "QP added.");

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
