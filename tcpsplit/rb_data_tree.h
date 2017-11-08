#include <linux/rbtree.h>
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
	};
	volatile struct socket *tx;
	volatile struct socket *rx;
};

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
		struct cbn_qp *data = container_of(node, struct cbn_qp, node);
		int result;

		result = strncmp(string, data->key, RB_KEY_LENGTH);

		if (result < 0)
			node = node->rb_left;
		else if (result > 0)
			node = node->rb_right;
		else
			return data;
	}

	return NULL;
}

static inline struct cbn_qp *add_rb_data(struct rb_root *root, struct cbn_qp *data)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;
	TRACE_PRINT("data %p new %p root %p\n", data, *new, root);
	show_key(data->key);
	TRACE_LINE();
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

	TRACE_LINE();
	/* Add new node and rebalance tree. */
	rb_link_node(&data->node, parent, new);
	rb_insert_color(&data->node, root);

	return NULL;
}
