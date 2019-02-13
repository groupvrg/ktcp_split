#ifndef __CBN_PRECONN_RB__TREE_H__
#define __CBN_PRECONN_RB__TREE_H__

#include <linux/rbtree.h>
#include <linux/types.h> //atomic_t
#include "cbn_common.h"

struct cbn_preconnection {
	struct rb_node		node;
	struct list_head 	list;
	int32_t			key;
};

static inline struct cbn_preconnection *search_rb_preconn(struct rb_root *root, int32_t key)
{
	struct rb_node *node = root->rb_node;

	while (node) {
		struct cbn_preconnection *this = container_of(node, struct cbn_preconnection, node);

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

static inline struct cbn_preconnection *add_rb_preconn(struct rb_root *root,
							struct cbn_preconnection *data)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;

	while (*new) {
		struct cbn_preconnection *this = container_of(*new, struct cbn_preconnection, node);
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
/*
 * TODO: R/W lock will be needed.
 * */
static inline struct cbn_preconnection *get_rb_preconn(struct rb_root *root, int32_t key,
							struct kmem_cache *cache, gfp_t flags)
{
	struct cbn_preconnection *preconn = search_rb_preconn(root, key);
	if (likely(preconn)) {
		return preconn;
	}
	preconn = kmem_cache_alloc(cache, flags);

	if (unlikely(!preconn))
		return NULL;

	preconn->key = key;
	INIT_LIST_HEAD(&preconn->list);
	if (add_rb_preconn(root, preconn)) {
		TRACE_ERROR("%s found a duplicate\n", __FUNCTION__);
		kmem_cache_free(cache, preconn);
		return NULL;
	}
	return preconn;
}
#endif /*__CBN_PRECONN_RB__TREE_H__*/
