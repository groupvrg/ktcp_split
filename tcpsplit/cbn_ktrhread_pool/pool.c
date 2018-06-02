#include <linux/init.h>      // included for __init and __exit macros
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <spinlock.h>
#include <linux/printk.h>

struct kthread_pool {
	int top_count;				//TODO: add spin_lock - need to protect lists and counters
	int running_count;
	int pool_size;				// TODO:Modify with debugfs or module param
	struct kmem_cache *pool_slab;
	struct task_struct *refil;
	struct list_head kthread_pool;
	struct list_head kthread_running;
	void (*pool_task)(void *data);		// TODO:just as easily can move to pool_elem
};

struct pool_elem {
	struct list_head list;
	struct task_struct *task;
	struct kthread_pool *pool;

	union {
		uint64_t _unspec[4];		// TODO:can be variable size, just need to tellcache_init
	};
};

#define DEF_CBN_POOL_SIZE 16
static struct kthread_pool cbn_pool = {.pool_size = DEF_CBN_POOL_SIZE};

static void kthread_pool_free(struct kthread_pool *cbn_pool, struct task_struct *task)
{
	struct pool_elem *elem = container_of(kthread_data(task), struct pool_elem, _unspec);

	list_add(&cbn_pool->kthread_pool, &elem->list);
	++cbn_pool->running_count;
}

static int pipe_loop_task(void *data)
{
	struct pool_elem *elem = data;
	struct kthread_pool *pool = elem->pool;

	while (!kthread_should_stop()) {
		pr_err("%s running\n", current->comm);
		if (pool->pool_task)
			pool->pool_task(data);

		pr_err("%s sleep\n", current->comm);
		set_current_state(TASK_INTERRUPTIBLE);
		kthread_pool_free(pool, elem->task);
		if (!kthread_should_stop()) {
			schedule();
		}
		__set_current_state(TASK_RUNNING);
	// TODO:
	//	consider adding some state? user might try freeing this struct, make sure its not running
	//	also consider frreing yourself if you are here...
	}
	pr_err("%s end\n", current->comm);
	return 0;
}

static int (*threadfn)(void *data) = pipe_loop_task;

static inline void refill_pool(struct kthread_pool *cbn_pool, int count)
{
	count = (count) ? count : cbn_pool->pool_size - cbn_pool->running_count;

	while (count--) {
		struct pool_elem *elem = kmem_cache_alloc(cbn_pool->pool_slab, GFP_KERNEL);
		struct task_struct *k
			= kthread_create(threadfn, elem->_unspec, "pool-thread-%d", cbn_pool->top_count);
		if (unlikely(!k)) {
			pr_err("failed to create kthread %d\n", cbn_pool->top_count);
			kmem_cache_free(cbn_pool->pool_slab, elem);
			return;
		}
		INIT_LIST_HEAD(&elem->list);
		elem->task = k;
		list_add(&elem->list, &cbn_pool->kthread_pool);
		pr_err("pool thread %d allocated %llx\n", cbn_pool->top_count, rdtsc());
		++cbn_pool->top_count;
	}
}

static void set_function( struct kthread_pool *pool,
			void (*pool_task)(void *data))
{
	pool->pool_task = pool_task;
}

static int refil_thread(void *data)
{
	struct kthread_pool *cbn_pool = data;

	while (!kthread_should_stop()) {
		refill_pool(cbn_pool, 0);

		set_current_state(TASK_INTERRUPTIBLE);
		if (!kthread_should_stop())
			schedule();
		__set_current_state(TASK_RUNNING);
	}
	pr_err("c ya...\n");
	return 0;
}

static struct task_struct *kthread_pool_alloc(struct kthread_pool *cbn_pool)
{
	struct pool_elem *elem = NULL;

	while (unlikely(list_empty(&cbn_pool->kthread_pool))) {
		pr_err("pool is empty refill is to slow\n");
		refill_pool(cbn_pool, 1);
	}

	elem = list_first_entry(&cbn_pool->kthread_pool, struct pool_elem, list);
	--cbn_pool->running_count;
	wake_up_process(cbn_pool->refil);
	return elem->task;
}

static void test(void)
{
	/*
	struct list_head *itr, *tmp;
	pr_err("test\n");

	list_for_each_safe(itr, tmp, &kthread_pool) {
		struct pool_elem *task = container_of(itr, struct pool_elem, list);

		wake_up_process(task->task);
	}
	*/
}

static int __init cbn_kthread_pool_init(void)
{
	pr_err("starting server_task\n");
	INIT_LIST_HEAD(&cbn_pool.kthread_pool);
	INIT_LIST_HEAD(&cbn_pool.kthread_running);
	cbn_pool.pool_slab = kmem_cache_create("pool-thread-cache",
						sizeof(struct pool_elem), 0, 0, NULL);

	cbn_pool.refil = kthread_run(refil_thread, &cbn_pool, "pool-cache-refill");
	//check for failure?
	test();
	return 0;
}

static void __exit cbn_kthread_pool_clean(void)
{
	struct list_head *itr, *tmp;
	pr_err("stopping server_task\n");

	list_for_each_safe(itr, tmp, &cbn_pool.kthread_pool) {
		struct pool_elem *task = container_of(itr, struct pool_elem, list);
		list_del(itr);
		kthread_stop(task->task);
		kmem_cache_free(cbn_pool.pool_slab, task);
	}

	list_for_each_safe(itr, tmp, &cbn_pool.kthread_running) {
		struct pool_elem *task = container_of(itr, struct pool_elem, list);
		list_del(itr);
		kthread_stop(task->task);
		kmem_cache_free(cbn_pool.pool_slab, task);
	}
	kmem_cache_destroy(cbn_pool.pool_slab);
	kthread_stop(cbn_pool.refil);
}

module_init(cbn_kthread_pool_init);
module_exit(cbn_kthread_pool_clean);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Markuze Alex");
MODULE_DESCRIPTION("CBN Kthread Pool");

