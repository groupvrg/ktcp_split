#include <linux/rbtree.h>

#define RB_KEY_LENGTH 12

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