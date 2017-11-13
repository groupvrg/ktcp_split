#include <linux/init.h>      // included for __init and __exit macros
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include "pool.h"

#include <linux/printk.h>

/* TODO: Consider how to reuse the pool threads
static void kthread_pool_free(struct kthread_pool *cbn_pool, struct pool_elem *elem)
{
	//struct pool_elem *elem = container_of(kthread_data(task), struct pool_elem, _unspec);
	kmem_cache_free(cbn_pool->pool_slab, elem);
}
*/

static void kthread_pool_reuse(struct kthread_pool *cbn_pool, struct pool_elem *elem)
{
	//struct pool_elem *elem = container_of(kthread_data(task), struct pool_elem, _unspec);
	list_del(&elem->list);
	list_add(&elem->list, &cbn_pool->kthread_pool);
	--cbn_pool->refil_needed;
}

static int pipe_loop_task(void *data)
{
	struct pool_elem *elem = data;
	struct kthread_pool *pool = elem->pool;

	while (!kthread_should_stop()) {
		pr_err("%s running\n", current->comm);
		if (elem->pool_task)
			elem->pool_task(elem->data);
		else
			pr_err("ERROR %s: no pool task\n", __FUNCTION__);

		pr_err("%s sleep\n", current->comm);
		set_current_state(TASK_INTERRUPTIBLE);
		if (!kthread_should_stop()) {
			kthread_pool_reuse(pool, elem);
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
	count = (count) ? count : cbn_pool->refil_needed;

	while (count--) {
		struct pool_elem *elem = kmem_cache_alloc(cbn_pool->pool_slab, GFP_KERNEL);
		struct task_struct *k
			= kthread_create(threadfn, elem, "pool-thread-%d", cbn_pool->top_count);
		if (unlikely(!k)) {
			pr_err("failed to create kthread %d\n", cbn_pool->top_count);
			kmem_cache_free(cbn_pool->pool_slab, elem);
			return;
		}
		INIT_LIST_HEAD(&elem->list);
		elem->task = k;
		list_add(&elem->list, &cbn_pool->kthread_pool);
		pr_err("pool thread %d [%p] allocated %llx\n", cbn_pool->top_count, elem, rdtsc());
		--cbn_pool->refil_needed;
		++cbn_pool->top_count;
	}
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
	return 0;
}

static struct pool_elem *kthread_pool_alloc(struct kthread_pool *cbn_pool)
{
	struct pool_elem *elem = NULL;

	while (unlikely(list_empty(&cbn_pool->kthread_pool))) {
		pr_err("pool is empty refill is to slow\n");
		refill_pool(cbn_pool, 1);
	}

	elem = list_first_entry(&cbn_pool->kthread_pool, struct pool_elem, list);
	list_del(&elem->list);
	++cbn_pool->refil_needed;
	//pr_err("allocated %p\n", elem);
	//wake_up_process(cbn_pool->refil);
	return elem;
}

struct pool_elem *kthread_pool_run(struct kthread_pool *cbn_pool, int (*func)(void *), void *data)
{
	struct pool_elem *elem = kthread_pool_alloc(cbn_pool);
	if (unlikely(!elem)) {
		pr_err("Failed to alloc elem\n");
		return ERR_PTR(-ENOMEM);
	}
	//pr_err("%s\n", __FUNCTION__);
	elem->pool_task = func;
	elem->data = data;
	list_add(&elem->list, &cbn_pool->kthread_running);
	wake_up_process(elem->task);
	return elem;
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

int __init cbn_kthread_pool_init(struct kthread_pool *cbn_pool)
{
	pr_err("starting server_task\n");
	INIT_LIST_HEAD(&cbn_pool->kthread_pool);
	INIT_LIST_HEAD(&cbn_pool->kthread_running);
	cbn_pool->pool_slab = kmem_cache_create("pool-thread-cache",
						sizeof(struct pool_elem), 0, 0, NULL);

	cbn_pool->refil_needed = cbn_pool->pool_size;
	cbn_pool->refil = kthread_run(refil_thread, cbn_pool, "pool-cache-refill");
	//check for failure?
	test();
	return 0;
}

void __exit cbn_kthread_pool_clean(struct kthread_pool *cbn_pool)
{
	struct list_head *itr, *tmp;
	pr_err("stopping server_task\n");

	list_for_each_safe(itr, tmp, &cbn_pool->kthread_pool) {
		struct pool_elem *task = container_of(itr, struct pool_elem, list);
		list_del(itr);
		kthread_stop(task->task);
		kmem_cache_free(cbn_pool->pool_slab, task);
	}

	list_for_each_safe(itr, tmp, &cbn_pool->kthread_running) {
		struct pool_elem *task = container_of(itr, struct pool_elem, list);
		list_del(itr);
		kthread_stop(task->task);
		kmem_cache_free(cbn_pool->pool_slab, task);
	}
	kmem_cache_destroy(cbn_pool->pool_slab);
	kthread_stop(cbn_pool->refil);
}

