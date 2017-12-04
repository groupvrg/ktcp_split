#ifndef __CBN_KTHREAD_POOL__
#define __CBN_KTHREAD_POOL__

struct kthread_pool {
	int top_count;				//TODO: add spin_lock - need to protect lists and counters
	int refil_needed;
	int pool_size;				// TODO:Modify with debugfs or module param
	struct kmem_cache *pool_slab;
	struct task_struct *refil;
	struct list_head kthread_pool;
	struct list_head kthread_running;
};

struct pool_elem {
	struct list_head list;
	struct kthread_pool *pool;
	struct task_struct *task;
	int (*pool_task)(void *data);
	void *data;

	union {
		uint64_t _unspec[2];		// TODO:can be variable size, just need to tell cache_init
	};
};

struct pool_elem *kthread_pool_run(struct kthread_pool *cbn_pool, int (*func)(void *), void *data);

int __init cbn_kthread_pool_init(struct kthread_pool *cbn_pool);
void __exit cbn_kthread_pool_clean(struct kthread_pool *cbn_pool);

#define DEF_CBN_POOL_SIZE 128

#endif /* __CBN_KTHREAD_POOL__ */
